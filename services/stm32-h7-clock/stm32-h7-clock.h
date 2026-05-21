/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32H7 clock tree
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include "stm32-clock-osc.h"
#include "stm32-clock-mux.h"
#include "stm32-clock-div.h"
#include "stm32-clock-mul.h"
#include "stm32-clock-gate.h"


typedef struct {
	Stm32ClockOsc hse_ck;
	Stm32ClockOsc hsi_ck;
	Stm32ClockOsc csi_ck;
	Stm32ClockMux pll_src_mux;
	Stm32ClockMux sys_ck_mux;
	Stm32ClockDiv pll1m_div;
	Stm32ClockMul pll1n_mul;
	Stm32ClockGate pll1p_gate;
	Stm32ClockDiv pll1p_div;
} Stm32Clock;

stm32_clock_ret_t stm32_h7_clock_init(Stm32Clock *self);
stm32_clock_ret_t stm32_h7_clock_free(Stm32Clock *self);
