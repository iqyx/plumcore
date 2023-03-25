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
#include <interfaces/sensor.h>
#include <services/adc-mcp3564/mcp3564.h>
#include <interfaces/mq.h>
#include <interfaces/clock.h>

#define ADC_COMPOSITE_MAX_MUX 4

#define VREF_MUX_CHANNEL_POS 0
#define VREF_MUX_CHANNEL_NEG 1

#define ADC_COMPOSITE_DEFAULT_TEMP_C (25.0f)

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

	float gain;
	float pregain;

	float offset_calib;
	float gain_calib;

	bool temp_compensation;
	float tc_a;
	float tc_b;
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
	Mux *vref_mux;

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

	Mq *mq;
	MqClient *mqc;

	Clock *clock;

	/* Temperature compensation */
	Sensor *device_temp;
	float temp_c;
	const char *temp_topic;

} AdcComposite;


adc_composite_ret_t adc_composite_init(AdcComposite *self, Adc *adc, Mq *mq);
adc_composite_ret_t adc_composite_start_cont(AdcComposite *self);
adc_composite_ret_t adc_composite_stop_cont(AdcComposite *self);
adc_composite_ret_t adc_composite_start_sequence(AdcComposite *self);
adc_composite_ret_t adc_composite_set_exc_power(AdcComposite *self, Power *exc_power);
adc_composite_ret_t adc_composite_set_vref_mux(AdcComposite *self, Mux *vref_mux);
adc_composite_ret_t adc_composite_set_clock(AdcComposite *self, Clock *clock);
adc_composite_ret_t adc_composite_set_device_temp(AdcComposite *self, Sensor *device_temp, const char *topic);


