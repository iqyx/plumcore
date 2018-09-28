/*
 * uMeshFw libopencm3 STM32 ADC module
 *
 * Copyright (C) 2016, Marek Koza, qyx@krtko.org
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

#include "module.h"
#include "interfaces/adc.h"


typedef enum {
	ADC_STM32_LOCM3_RET_OK = 0,
	ADC_STM32_LOCM3_RET_FAILED = -1,
} adc_stm32_locm3_ret_t;


typedef struct {

	Module module;
	IAdc iface;

	/**
	 * libopencm3 ADC port
	 */
	uint32_t adc;

} AdcStm32Locm3;


adc_stm32_locm3_ret_t adc_stm32_locm3_init(AdcStm32Locm3 *self, uint32_t adc);
adc_stm32_locm3_ret_t adc_stm32_locm3_free(AdcStm32Locm3 *self);

