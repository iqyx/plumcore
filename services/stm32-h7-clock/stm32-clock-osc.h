/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 oscillator clock node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "stm32-clock-common.h"


struct stm32_clock_osc_config {
	const char *name;
	uint32_t fixed_rate_hz;
	uint32_t min_rate_hz;
	uint32_t max_rate_hz;
	struct stm32_clock_reg reg_enable;
	struct stm32_clock_reg reg_ready;
};

typedef struct stm32_clock_osc {
	struct stm32_clock_osc_config config;
	Clock clock;
	uint32_t rate_hz;
	uint32_t refcnt;
} Stm32ClockOsc;

stm32_clock_ret_t stm32_clock_osc_init(Stm32ClockOsc *self, const struct stm32_clock_osc_config *config);
