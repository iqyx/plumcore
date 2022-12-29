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
	self->exc_voltage_v = 3.0f;

	return ADC_COMPOSITE_RET_OK;
}


adc_composite_ret_t adc_composite_start_cont(AdcComposite *self) {
	if (self->state != ADC_COMPOSITE_STOP) {
		return ADC_COMPOSITE_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("starting continuous sampling"));

	xTaskCreate(adc_composite_cont_task, "ws-source", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->cont_task));
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


adc_composite_ret_t adc_composite_start_sequence(AdcComposite *self) {
	/* Iterate over all channels. */
	const struct adc_composite_channel *c = *(self->channels);
	while (c->name != NULL) {
		set_muxes(self, c->muxes);
		/** @todo compute exactly how much time is needed to stabilise the input */
		/** @todo save the current sample time */

		/* Enable positive excitation voltage and switch the Vref mux appropriately. */
		if (self->exc_power != NULL) {
			self->exc_power->vmt->set_voltage(self->exc_power, self->exc_voltage_v);
			/** @todo remove the shortcut */
			gpio_clear(VREF1_SEL_PORT, VREF1_SEL_PIN);
			gpio_clear(VREF2_SEL_PORT, VREF2_SEL_PIN);
		}

		/* Measure the channel using positive excitation and positive Vref mux. */
		uint8_t status = 0;
		int32_t v1 = 0;
		/** @todo measure multiple times until the input is stable */
		mcp_measure(self->adc, &status, &v1);
		mcp_measure(self->adc, &status, &v1);
		mcp_measure(self->adc, &status, &v1);

		if (c->ac_excitation) {

			/* Switch excitation voltage and the corresponding mux to negative. */
			if (self->exc_power != NULL) {
				self->exc_power->vmt->set_voltage(self->exc_power, -self->exc_voltage_v);
				/** @todo remove the shortcut */
				gpio_set(VREF1_SEL_PORT, VREF1_SEL_PIN);
				gpio_set(VREF2_SEL_PORT, VREF2_SEL_PIN);
			}

			int32_t v2 = 0;
			/** @todo measure multiple times until the input is stable */
			mcp_measure(self->adc, &status, &v2);
			mcp_measure(self->adc, &status, &v2);
			mcp_measure(self->adc, &status, &v2);

			/* Aggregate the two results */
			v1 = (v1 - v2) / 2;
		}

		NdArray array;
		ndarray_init_view(&array, DTYPE_INT32, 1, &v1, sizeof(v1));

		/** @todo get the exact sample time from somewhere */
		struct timespec ts = {0};

		/* Publish the array and clear the channel buffer. */
		self->mqc->vmt->publish(self->mqc, c->name, &array, &ts);

		c++;
	}

	return ADC_COMPOSITE_RET_OK;
}


adc_composite_ret_t adc_composite_set_exc_power(AdcComposite *self, Power *exc_power) {
	self->exc_power = exc_power;
	return ADC_COMPOSITE_RET_OK;
}


