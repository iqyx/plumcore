/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 generic clock divider node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "stm32-clock-common.h"


struct stm32_clock_div_config {
	const char *name;
	Clock *parent;
	struct stm32_clock_reg reg_div;
	int32_t offset_div;
	uint32_t min_rate_hz;
	uint32_t max_rate_hz;
	uint32_t min_prescaler;
	uint32_t max_prescaler;
	/* Optional enable/disable register bit. When reg is NULL, enable and disable
	 * only manage the parent reference count without touching any hardware register. */
	struct stm32_clock_reg reg_enable;
};

typedef struct stm32_clock_div {
	struct stm32_clock_div_config config;

	Clock clock;

	/* This is the value we are trying to get. It is being set even if the
	 * divider is disabled. */
	uint32_t rate_hz;

	uint32_t refcnt;
} Stm32ClockDiv;

stm32_clock_ret_t stm32_clock_div_init(Stm32ClockDiv *self, const struct stm32_clock_div_config *config);
