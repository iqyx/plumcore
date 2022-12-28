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

#include "adc-composite.h"

#define MODULE_NAME "adc-composite"
/**
 * @todo
 *
 * - pripravit hlavny proces, start_continuous, ukoncenie DONE
 * - v cont rezime sa bude pouzivat zatial mcp device priamo, neskor sa zameni za Adc device DONE
 * - ked sa dosiahne stav, ze vie samplovat aspon jeden kanal, pridat waveform source interface
 * - skusit v port.c instanciovat ws_source a poslat to do MQ
 * - zistit, preco nejde MQ sniff & spol
 * - pokracovat, ked sa podari kontinualne samplovat jeden kanal a ziskat vysledky na MQ
 * - pripravit locm3-mux servis, staticky nastavovat jeden kanal, otestovat funkcnost
 * - pridat moznost samplovat viac kanalov, sucasne nastavovat viac muxov
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

	/* Enable excitation switch/regulator and aux ADC power switch/regulator. */
	if (self->power_switch != NULL) {
		self->power_switch->vmt->enable(self->power_switch, true);
	}
	if (self->exc_power != NULL) {
		self->exc_power->vmt->enable(self->exc_power, true);
	}

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

	vTaskDelete(NULL);
}



adc_composite_ret_t adc_composite_init(AdcComposite *self, Adc *adc) {
	memset(self, 0, sizeof(AdcComposite));
	// self->adc = adc;

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
	/** @todo set channel muxes */
}


adc_composite_ret_t adc_composite_start_sequence(AdcComposite *self) {
	/* Iterate over all channels. */
	const struct adc_composite_channel *c = *(self->channels);
	while (c->name != NULL) {
		set_muxes(self, *(c->muxes));
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
			mcp_measure(self->adc, &status, &v2);

			/* Aggregate the two results */
			v1 = (v1 - v2) / 2;
		}

		/** @todo publish the channel result */
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("channel %s = %d"), c->name, v1);

		c++;
	}
	/* Set excitation voltage to 0 V to save power at the end of the sequence. */
	if (self->exc_power != NULL) {
		// self->exc_power->vmt->set_voltage(self->exc_power, 0.0f);
	}

	return ADC_COMPOSITE_RET_OK;
}


adc_composite_ret_t adc_composite_set_exc_power(AdcComposite *self, Power *exc_power) {
	self->exc_power = exc_power;
	return ADC_COMPOSITE_RET_OK;
}


