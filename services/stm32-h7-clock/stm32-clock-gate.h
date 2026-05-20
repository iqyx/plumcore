/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 clock gate node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "stm32-clock-common.h"


struct stm32_clock_gate_config {
	const char *name;
	Clock *parent;
	struct stm32_clock_reg reg_enable;
};

typedef struct stm32_clock_gate {
	struct stm32_clock_gate_config config;

	Clock clock;

	uint32_t refcnt;
} Stm32ClockGate;

stm32_clock_ret_t stm32_clock_gate_init(Stm32ClockGate *self, const struct stm32_clock_gate_config *config);
