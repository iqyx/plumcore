/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 clock node common types
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <main.h>


typedef enum {
	STM32_CLOCK_RET_OK = 0,
	STM32_CLOCK_RET_FAILED,
} stm32_clock_ret_t;


struct stm32_clock_reg {
	volatile uint32_t *reg;
	uint32_t mask;
	uint32_t shift;
};


#define CLOCK_PREFIX(self, x) "\x1b[33m" "%s/%s: " "\x1b[0m" x, MODULE_NAME, (self)->clock.name
