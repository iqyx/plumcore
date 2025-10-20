/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Driver for STM32 internal flash memory
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <main.h>

#include <libopencm3/cm3/common.h>
#include <interfaces/flash.h>

#define STM32_FLASH_BASE 0x08000000


typedef enum {
	STM32_FLASH_RET_OK = 0,
	STM32_FLASH_RET_FAILED,
	STM32_FLASH_RET_NULL,
} stm32_flash_ret_t;


typedef struct {
	bool initialized;
	size_t flash_size;
	size_t flash_sector_size;
	size_t flash_page_size;

	Flash flash;
} Stm32Flash;


stm32_flash_ret_t stm32_flash_init(Stm32Flash *self);
stm32_flash_ret_t stm32_flash_free(Stm32Flash *self);
