/* SPDX-License-Identifier: BSD-2-Clause
 *
 * High level composite ADC device
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>

#include <interfaces/mux.h>
#include <interfaces/adc.h>
#include <interfaces/power.h>
#include <services/adc-mcp3564/mcp3564.h>

#define ADC_COMPOSITE_MAX_MUX 4

typedef enum  {
	ADC_COMPOSITE_RET_OK = 0,
	ADC_COMPOSITE_RET_FAILED,
} adc_composite_ret_t;


struct adc_composite_mux {
	Mux *mux;
	uint32_t channel;
};

struct adc_composite_channel {
	/* Before sampling of the channel, the following muxes have to be set. The last one is NULL. */
	const struct adc_composite_mux muxes[ADC_COMPOSITE_MAX_MUX];
	const char *name;
	bool ac_excitation;
};

enum adc_composite_state {
	ADC_COMPOSITE_STOP = 0,
	ADC_COMPOSITE_RUN_SINGLE,
	ADC_COMPOSITE_RUN_SEQUENCE,
	ADC_COMPOSITE_RUN_CONT,
	ADC_COMPOSITE_RUN_CONT_REQ_STOP,
};

typedef struct adc_composite {
	/* Before the measurement, an excitation voltage can be applied. Its amplitude and polarity
	 * can be controlled using a Power device. Do not manipulate excitation voltage if NULL. */
	Power *exc_power;
	float exc_voltage_v;

	/* Reference voltage mux for AC excitation. Do not use if NULL. */
	// Mux *vref_mux;

	/* Some parts of the composite ADC subsystem can be enabled on-demand using a dedicated Power device.
	 * DO not power on/off anything if NULL. */
	Power *power_switch;

	/* Channels configured for sequential sampling. */
	const struct adc_composite_channel (*channels)[];

	/* ADC used for sampling the input signal. */
	// Adc *adc;
	/** @todo remove the shortcut */
	Mcp3564 *adc;

	/* ADC interface host can be used to request sampling of a single channel or a sequence of channels. */
	Adc adc_iface;

	/** @todo LED device to blink during measurement */

	/* Buffer for conversion results. */
	uint8_t *buf;
	size_t buf_size;

	volatile enum adc_composite_state state;
	uint32_t interval_ms;
	TaskHandle_t cont_task;


} AdcComposite;


adc_composite_ret_t adc_composite_init(AdcComposite *self, Adc *adc);
adc_composite_ret_t adc_composite_start_cont(AdcComposite *self);
adc_composite_ret_t adc_composite_stop_cont(AdcComposite *self);
adc_composite_ret_t adc_composite_start_sequence(AdcComposite *self);
adc_composite_ret_t adc_composite_set_exc_power(AdcComposite *self, Power *exc_power);
