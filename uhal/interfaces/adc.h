/*
 * uMeshFw HAL ADC interface
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
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
#include "interface.h"


typedef enum {
	INTERFACE_ADC_RET_OK = 0,
	INTERFACE_ADC_RET_FAILED = -1,
} interface_adc_ret_t;

struct interface_adc_vmt {
	int32_t (*sample)(void *context, uint32_t input, int32_t *sample);
	void *context;
};

typedef struct {
	Interface interface;

	struct interface_adc_vmt vmt;

} IAdc;


interface_adc_ret_t interface_adc_init(IAdc *self);
interface_adc_ret_t interface_adc_sample(IAdc *self, uint32_t input, int32_t *sample);


