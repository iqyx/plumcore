/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Sensor device driver using an ADC input
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include <interfaces/sensor.h>
#include <interfaces/adc.h>
#include "adc-sensor.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "adc-sensor"


static sensor_ret_t adc_sensor_value_f(Sensor *sensor, float *value) {
	AdcSensor *self = (AdcSensor *)sensor;

	float f = 0.0f;
	for (uint32_t i = 0; i < self->oversample; i++) {
		adc_sample_t v = 0;
		if (self->adc->vmt->convert_single(self->adc, self->input, &v) != ADC_RET_OK) {
			return SENSOR_RET_FAILED;
		}
		f += (float)v;
	}
	f /= (float)self->oversample;

	/* Resistor divider computation */
	if (self->div_low_fs != 0.0f) {
		f = (f * self->div_low_high_r) / (self->div_low_fs - f);
	}

	/* Apply the conversion */
	if (self->a != 0.0f || self->b != 0.0f || self->c != 0.0f) {
		f = self->a * f * f + self->b * f + self->c;
	}

	if (value != NULL) {
		*value = f;
	}

	return SENSOR_RET_OK;
}


static const struct sensor_vmt adc_sensor_vmt = {
	.value_f = adc_sensor_value_f,
};


adc_sensor_ret_t adc_sensor_init(AdcSensor *self, Adc *adc, uint32_t input, const struct sensor_info *info) {
	memset(self, 0, sizeof(AdcSensor));

	if (adc == NULL || info == NULL) {
		return ADC_SENSOR_RET_FAILED;
	}

	self->adc = adc;
	self->input = input;
	self->oversample = 1;

	self->iface.vmt = &adc_sensor_vmt;
	self->iface.info = info;
	self->iface.parent = self;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("sensor initialised on channel %u"), input);
	return ADC_SENSOR_RET_OK;
}


adc_sensor_ret_t adc_sensor_free(AdcSensor *self) {
	(void)self;
	return ADC_SENSOR_RET_OK;
}

