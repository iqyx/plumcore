/*
 * Create a sensor device using ADC input
 *
 * Copyright (C) 2018, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "interfaces/sensor.h"
#include "interfaces/adc.h"
#include "adc-sensor.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "adc-sensor"


static interface_sensor_ret_t adc_sensor_iface_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	AdcSensor *self = (AdcSensor *)context;
	memcpy(info, &self->info, sizeof(ISensorInfo));

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t adc_sensor_iface_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	AdcSensor *self = (AdcSensor *)context;

	/* Get ADC value in microvolts. */
	int32_t sample = 0;
	iadc_sample(self->adc, self->input, &sample);

	*value = (float)sample / 1000000.0 * (float)self->multiplier / (float)self->divider;

	return INTERFACE_SENSOR_RET_OK;
}


adc_sensor_ret_t adc_sensor_init(AdcSensor *self, IAdc *adc, uint32_t input, ISensorInfo *info, int32_t multiplier, int32_t divider) {
	if (u_assert(self != NULL)) {
		return ADC_SENSOR_RET_FAILED;
	}

	memset(self, 0, sizeof(AdcSensor));
	self->adc = adc;
	self->input = input;
	self->multiplier = multiplier;
	self->divider = divider;
	memcpy(&self->info, info, sizeof(ISensorInfo));

	interface_sensor_init(&(self->iface));
	self->iface.vmt.info = adc_sensor_iface_info;
	self->iface.vmt.value = adc_sensor_iface_value;
	self->iface.vmt.context = (void *)self;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module initialized"));
	return ADC_SENSOR_RET_OK;
}


adc_sensor_ret_t adc_sensor_free(AdcSensor *self) {
	if (u_assert(self != NULL)) {
		return ADC_SENSOR_RET_FAILED;
	}

	return ADC_SENSOR_RET_OK;
}

