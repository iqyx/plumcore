/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 clock manager service
 *
 * Beware, currently STM32G4 compatibility only, PoC, see stm32-clock.rst.
 *
 * Copyright (c) 2023-2024, Marek Koza (qyx@krtko.org)
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

enum stm32_clock_src {
	STM32_CLOCK_SRC_NONE = 0,
	STM32_CLOCK_SRC_LSE,
	STM32_CLOCK_SRC_LSI,
	STM32_CLOCK_SRC_HSE,
	STM32_CLOCK_SRC_HSI16,
	STM32_CLOCK_SRC_HSI48,
	STM32_CLOCK_SRC_MSI,
};

enum stm32_clock_state {
	STM32_CLOCK_STATE_DEFAULT = 0,
	STM32_CLOCK_STATE_INIT_LSE,
	STM32_CLOCK_STATE_WAIT_LSE,
	STM32_CLOCK_STATE_LSE_OK,
	STM32_CLOCK_STATE_LSE_DONE,

	STM32_CLOCK_STATE_INIT_HSI16,
	STM32_CLOCK_STATE_WAIT_HSI16,
	STM32_CLOCK_STATE_HSI16_OK,

	STM32_CLOCK_STATE_INIT_HSE,
	STM32_CLOCK_STATE_WAIT_HSE,
	STM32_CLOCK_STATE_HSE_OK,

	STM32_CLOCK_STATE_SYSCLK,

	STM32_CLOCK_STATE_INIT_DONE,
	STM32_CLOCK_STATE_CHECK,
	STM32_CLOCK_STATE_FAILED,
};

struct stm32_clock_pll_conf {
	uint32_t pllm, plln, pllp, pllq, pllr;
};

enum stm32_clock_level {
	STM32_CLOCK_LEVEL_LOW_POWER,
	STM32_CLOCK_LEVEL_LOW_PERF,
	STM32_CLOCK_LEVEL_MEDIUM_PERF,
	STM32_CLOCK_LEVEL_HIGH_PERF,
};

struct stm32_clock_conf {
	uint32_t sysclk;
	uint8_t vcore_range;
	bool vcore_boost;
	uint8_t flash_wait_states;
};

typedef struct stm32_clock {
	volatile enum stm32_clock_state state;
	TaskHandle_t handler_task;
	SemaphoreHandle_t init_done;
	uint32_t step_interval_ms;
	uint32_t rough_time_ms;
	uint32_t timeout_ms;

	uint32_t tim_ker_ck;
	uint32_t freq_nom_lse_hz;
	uint32_t freq_lse_hz;
	uint32_t freq_hsi16_hz;
	uint32_t freq_hse_hz;
	uint32_t freq_nom_hse_hz;
	uint32_t freq_sysclk_hz;

	enum stm32_clock_level req_level;

} Stm32Clock;



stm32_clock_ret_t stm32_clock_init(Stm32Clock *self, enum stm32_clock_level level);
stm32_clock_ret_t stm32_clock_free(Stm32Clock *self);
stm32_clock_ret_t stm32_clock_wait_init_done(Stm32Clock *self);
