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
#include <blake2.h>
#include <interfaces/flash.h>

/* This is a naive approach. Some STM32 families have non-uniform flash sectors. */
#if defined(STM32G4)
	#define STM32_FLASH_SIZE ((uint32_t)MMIO16(0x1fff75e0) << 10UL)
	#define STM32_FLASH_SECTOR_SIZE 2048
	#define STM32_FLASH_PAGE_SIZE 8
	#define STM32_FLASH_BASE 0x08000000
#else
	#error "No device electronic signature flash size register defined for this MCU family."
#endif


typedef enum {
	STM32_FLASH_RET_OK = 0,
	STM32_FLASH_RET_FAILED,
	STM32_FLASH_RET_NULL,
} stm32_flash_ret_t;


typedef struct {
	bool initialized;

	Flash flash;
} Stm32Flash;


stm32_flash_ret_t stm32_flash_init(Stm32Flash *self);
stm32_flash_ret_t stm32_flash_free(Stm32Flash *self);
