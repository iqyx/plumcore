/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 HSE clock node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "stm32-clock-common.h"


typedef struct stm32_clock_hse {
	Clock clock;
	uint32_t rate_hz;
	uint32_t refcnt;
} Stm32ClockHse;

stm32_clock_ret_t stm32_clock_hse_init(Stm32ClockHse *self, uint32_t rate_hz, bool bypass);
