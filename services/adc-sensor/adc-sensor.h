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

#pragma once

#include <stdint.h>

#include "main.h"
#include "interfaces/sensor.h"
#include "interfaces/adc.h"


typedef enum {
	ADC_SENSOR_RET_OK = 0,
	ADC_SENSOR_RET_FAILED = -1,
} adc_sensor_ret_t;


typedef struct {
	ISensor iface;
	IAdc *adc;
	uint32_t input;

	int32_t multiplier;
	int32_t divider;
	ISensorInfo info;

} AdcSensor;


adc_sensor_ret_t adc_sensor_init(AdcSensor *self, IAdc *adc, uint32_t input, ISensorInfo *info, int32_t multiplier, int32_t divider);
adc_sensor_ret_t adc_sensor_free(AdcSensor *self);


