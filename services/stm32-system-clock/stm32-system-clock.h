/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * System clock counting microseconds using STM32 hardware.
 *
 * Copyright (c) 2018-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include <interfaces/clock.h>
#include <main.h>



typedef enum {
	STM32_SYSTEM_CLOCK_RET_OK = 0,
	STM32_SYSTEM_CLOCK_RET_FAILED,
	STM32_SYSTEM_CLOCK_RET_NULL,
} stm32_system_clock_ret_t;


typedef struct {
	bool initialized;
	void *timer_base;
	uint32_t freq_hz;

	/* Manipulated from the irq handler. */
	volatile uint32_t overflows;

	Clock clock;

} Stm32SystemClock;


stm32_system_clock_ret_t stm32_system_clock_init(Stm32SystemClock *self, void *timer_base, uint32_t freq_hz);
stm32_system_clock_ret_t stm32_system_clock_free(Stm32SystemClock *self);
stm32_system_clock_ret_t stm32_system_clock_irq_handler(Stm32SystemClock *self);

