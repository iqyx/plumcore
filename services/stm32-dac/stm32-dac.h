/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 DAC device
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/dac.h>

typedef enum  {
	STM32_DAC_RET_OK = 0,
	STM32_DAC_RET_FAILED,
} stm32_dac_ret_t;


typedef struct stm32_dac {
	/* libopencm3 DAC handle */
	uint32_t locm3_dac;
	uint32_t locm3_dac_channel;

	Dac dac_iface;

} Stm32Dac;


stm32_dac_ret_t stm32_dac_init(Stm32Dac *self, uint32_t dac, uint32_t channel);
stm32_dac_ret_t stm32_dac_free(Stm32Dac *self);

