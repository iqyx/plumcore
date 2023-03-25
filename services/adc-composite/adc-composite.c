/* SPDX-License-Identifier: BSD-2-Clause
 *
 * High level composite ADC device
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <libopencm3/stm32/gpio.h>
#include <types/ndarray.h>
#include <interfaces/servicelocator.h>

#include "adc-composite.h"

#define MODULE_NAME "adc-composite"
/**
 * @todo
 *
 * - postovat priamo na MQ, waveform source nema prilis zmysel
 * - kazdy kanal ma buffer, aby sa nepostovala kazda hodnota individualne
 * - zistit, preco nejde MQ sniff & spol
 * - overit, ze sa vysledky korektne presuvaju na MQ
 * - pridat agregator na vysledky na MQ, jedna hodnota raz za minutu
 */


static void mcp_measure(Mcp3564 *self, uint8_t *status, int32_t *value) {
	mcp3564_send_cmd(self, MCP3564_CMD_START, status, NULL, 0, NULL, 0);
	/* Wait for data ready. */
	/** @todo timeout, remove the shortcut */
	while (gpio_get(ADC_IRQ_PORT, ADC_IRQ_PIN)) {
		;
	}
	mcp3564_read_reg(self, MCP3564_REG_ADCDATA, 4, (uint32_t *)value, status);
}


static void adc_composite_cont_task(void *p) {
	AdcComposite *self = (AdcComposite *)p;

	if (self->channels == NULL) {
		goto err;
	}

	/* Try to connect to the message queue. If it fails, do not measure anything as there is
	 * nothing to publish the results to. */
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot connect to the message queue"));
		goto err;
	}

	/* Enable excitation switch/regulator and aux ADC power switch/regulator. */
	if (self->power_switch != NULL) {
		self->power_switch->vmt->enable(self->power_switch, true);
	}
	if (self->exc_power != NULL) {
		self->exc_power->vmt->enable(self->exc_power, true);
	}
	/** @todo check the time required for the Vref to stabilise. Too short time may invalidate first results.
	 *        Do not forget to switch the Vref mux correctly, otherwise the Vref may be negative. */

	self->state = ADC_COMPOSITE_RUN_CONT;
	while (self->state != ADC_COMPOSITE_RUN_CONT_REQ_STOP) {
		adc_composite_start_sequence(self);

		vTaskDelay(self->interval_ms);
	}
err:
	self->state = ADC_COMPOSITE_STOP;

	/* Disable until the next task is started. */
	if (self->exc_power != NULL) {
		self->exc_power->vmt->enable(self->exc_power, false);
	}
	if (self->power_switch != NULL) {
		self->power_switch->vmt->enable(self->power_switch, false);
	}
	if (self->mqc != NULL) {
		self->mqc->vmt->close(self->mqc);
	}

	vTaskDelete(NULL);
}



adc_composite_ret_t adc_composite_init(AdcComposite *self, Adc *adc, Mq *mq) {
	memset(self, 0, sizeof(AdcComposite));
	// self->adc = adc;
	self->mq = mq;

	self->interval_ms = 500;
	self->exc_voltage_v = 3.3f;

	return ADC_COMPOSITE_RET_OK;
}


adc_composite_ret_t adc_composite_start_cont(AdcComposite *self) {
	if (self->state != ADC_COMPOSITE_STOP) {
		return ADC_COMPOSITE_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("starting continuous sampling"));

	xTaskCreate(adc_composite_cont_task, "adc-comp", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->cont_task));
	if (self->cont_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return ADC_COMPOSITE_RET_FAILED;
	}
	return ADC_COMPOSITE_RET_OK;
}


adc_composite_ret_t adc_composite_stop_cont(AdcComposite *self) {
	if (self->state != ADC_COMPOSITE_RUN_CONT) {
		return ADC_COMPOSITE_RET_FAILED;
	}
	self->state = ADC_COMPOSITE_RUN_CONT_REQ_STOP;
	while (self->state != ADC_COMPOSITE_STOP) {
		vTaskDelay(10);
	}
	/** @todo handle timeout */
	return ADC_COMPOSITE_RET_OK;
}


static void set_muxes(AdcComposite *self, const struct adc_composite_mux m[]) {
	while (m->mux != NULL) {
		m->mux->vmt->select(m->mux, m->channel);
		m++;
	}
}


static adc_composite_ret_t process_sample(AdcComposite *self, const struct adc_composite_channel *channel, int32_t sample, struct timespec *ts) {
	/* Adc composite is always processing results in uV/V */
	float f = sample / 8388607.0f * 1000000.0f;

	/* Apply the current gain settings. */
	if (channel->gain != 0.0f) {
		f /= channel->gain;
	}
	if (channel->pregain != 0.0f) {
		f /= channel->pregain;
	}

	/* Perform offset and gain adjustment if requested. */
	f += channel->offset_calib;
	if (channel->gain_calib != 0.0f) {
		f *= channel->gain_calib;
	}

	/* And finally do a temperature compensation if requested. */
	if (channel->temp_compensation) {
		f /= (channel->tc_a * self->temp_c * self->temp_c + channel->tc_b * self->temp_c + 1.0f);
	}

	NdArray array;
	ndarray_init_view(&array, DTYPE_FLOAT, 1, &f, sizeof(f));

	/* Publish the array and clear the channel buffer. */
	self->mqc->vmt->publish(self->mqc, channel->name, &array, ts);
}


static adc_composite_ret_t measure_temp(AdcComposite *self) {
	if (self->device_temp != NULL && self->device_temp->vmt->value_f != NULL) {
		self->device_temp->vmt->value_f(self->device_temp, &self->temp_c);

		if (self->temp_topic != NULL) {
			struct timespec ts = {0};
			if (self->clock != NULL) {
				self->clock->get(self->clock->parent, &ts);
			}

			NdArray array;
			ndarray_init_view(&array, DTYPE_FLOAT, 1, &self->temp_c, sizeof(float));

			self->mqc->vmt->publish(self->mqc, self->temp_topic, &array, &ts);
		}
	} else {
		self->temp_c = ADC_COMPOSITE_DEFAULT_TEMP_C;
	}
}


adc_composite_ret_t adc_composite_start_sequence(AdcComposite *self) {
	/* Measure the device temperature at the beginning of the sequence. */
	measure_temp(self);

	/* Iterate over all channels. */
	const struct adc_composite_channel *c = *(self->channels);
	while (c->name != NULL) {
		set_muxes(self, c->muxes);
		/** @todo compute exactly how much time is needed to stabilise the input */
		/** @todo save the current sample time */

		/* Enable positive excitation voltage and switch the Vref mux appropriately. */
		if (self->exc_power != NULL) {
			self->exc_power->vmt->set_voltage(self->exc_power, self->exc_voltage_v);
		}
		/* If the reference muxing is enabled, set the mux to positive regardless of the
		 * AC excitation setting. */
		if (self->vref_mux != NULL) {
			/** @todo when to disable the mux? */
			self->vref_mux->vmt->enable(self->vref_mux, true);
			self->vref_mux->vmt->select(self->vref_mux, VREF_MUX_CHANNEL_POS);
		}

		struct timespec ts = {0};
		if (self->clock != NULL) {
			self->clock->get(self->clock->parent, &ts);
		}

		/* Measure the channel using positive excitation and positive Vref mux. */
		uint8_t status = 0;
		volatile int32_t v1 = 0;
		/** @todo measure multiple times until the input is stable */
		mcp_measure(self->adc, &status, &v1);
		mcp_measure(self->adc, &status, &v1);
		mcp_measure(self->adc, &status, &v1);

		if (c->ac_excitation) {
			/* Switch excitation voltage and the corresponding mux to negative. */
			if (self->exc_power != NULL) {
				/* See the minus unary operator here. */
				self->exc_power->vmt->set_voltage(self->exc_power, -self->exc_voltage_v);
			}
			if (self->vref_mux != NULL) {
				self->vref_mux->vmt->select(self->vref_mux, VREF_MUX_CHANNEL_NEG);
			}

			int32_t v2 = 0;
			/** @todo measure multiple times until the input is stable */
			mcp_measure(self->adc, &status, &v2);
			mcp_measure(self->adc, &status, &v2);
			mcp_measure(self->adc, &status, &v2);

			/* Aggregate the two results */
			v1 = (v1 - v2) / 2;
		}

		process_sample(self, c, v1, &ts);

		c++;
	}

	return ADC_COMPOSITE_RET_OK;
}


adc_composite_ret_t adc_composite_set_exc_power(AdcComposite *self, Power *exc_power) {
	self->exc_power = exc_power;
	return ADC_COMPOSITE_RET_OK;
}


adc_composite_ret_t adc_composite_set_vref_mux(AdcComposite *self, Mux *vref_mux) {
	self->vref_mux = vref_mux;
	return ADC_COMPOSITE_RET_OK;
}


adc_composite_ret_t adc_composite_set_clock(AdcComposite *self, Clock *clock) {
	self->clock = clock;
	return ADC_COMPOSITE_RET_OK;
}

adc_composite_ret_t adc_composite_set_device_temp(AdcComposite *self, Sensor *device_temp, const char *topic) {
	self->device_temp = device_temp;
	self->temp_topic = topic;
	return ADC_COMPOSITE_RET_OK;
}
