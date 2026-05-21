/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 generic clock mux node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "stm32-clock-common.h"


#define STM32_CLOCK_MUX_MAX_INPUTS 8

struct stm32_clock_mux_config {
	const char *name;
	Clock *inputs[STM32_CLOCK_MUX_MAX_INPUTS];
	size_t input_count;
	volatile uint32_t *reg;
	uint32_t mask;
	uint32_t shift;
	uint32_t disabled_id;
};

typedef struct stm32_clock_mux {
	struct stm32_clock_mux_config config;

	Clock clock;

	Clock *current_parent;
	uint32_t refcnt;
} Stm32ClockMux;

stm32_clock_ret_t stm32_clock_mux_init(Stm32ClockMux *self, const struct stm32_clock_mux_config *config);
