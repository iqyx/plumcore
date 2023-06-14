/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Sensor device driver using an ADC input
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <main.h>
#include <interfaces/sensor.h>
#include <interfaces/adc.h>


typedef enum {
	ADC_SENSOR_RET_OK = 0,
	ADC_SENSOR_RET_FAILED = -1,
} adc_sensor_ret_t;


typedef struct {
	Sensor iface;
	Adc *adc;
	uint32_t input;

	uint32_t oversample;

	/* y = ax2 + bx + c */
	float a, b, c;

	float ntc_r25;
	float ntc_beta;

	/* Resistor divider with the unknown resistance on the low side */
	float div_low_fs;
	float div_low_high_r;

} AdcSensor;


adc_sensor_ret_t adc_sensor_init(AdcSensor *self, Adc *adc, uint32_t input, const struct sensor_info *info);
adc_sensor_ret_t adc_sensor_free(AdcSensor *self);


