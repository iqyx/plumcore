/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 internal ADC driver
 *
 * Copyright (c) 2016-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <interfaces/adc.h>


typedef enum {
	STM32_ADC_RET_OK = 0,
	STM32_ADC_RET_FAILED = -1,
} stm32_adc_ret_t;


typedef struct {
	Adc iface;

	/** @todo emove dependency on libopencm3 */
	uint32_t adc;

} Stm32Adc;


stm32_adc_ret_t stm32_adc_init(Stm32Adc *self, uint32_t adc);
stm32_adc_ret_t stm32_adc_free(Stm32Adc *self);

