/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 PLL multiplier node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "stm32-clock-common.h"


struct stm32_clock_mul_config {
	const char *name;
	Clock *parent;

	struct stm32_clock_reg reg_plln;
	int32_t offset_plln;

	struct stm32_clock_reg reg_enable;
	struct stm32_clock_reg reg_ready;

	uint32_t min_multiplier;
	uint32_t max_multiplier;

	uint32_t min_rate_hz;
	uint32_t max_rate_hz;
};

typedef struct stm32_clock_mul {
	struct stm32_clock_mul_config config;

	Clock clock;

	uint32_t rate_hz;

	uint32_t refcnt;
} Stm32ClockMul;

stm32_clock_ret_t stm32_clock_mul_init(Stm32ClockMul *self, const struct stm32_clock_mul_config *config);
