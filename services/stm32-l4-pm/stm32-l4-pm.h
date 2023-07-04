/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Power manager for the STM32L4 MCU family
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/pm.h>

/** @todo make Kconfig for these values */
#define STM32_L4_PM_MAX_STOP1_TIME 1000
#define STM32_L4_PM_LPTIM_ARR 0x8000
#define STM32_L4_PM_LPTIM LPTIM1
#define STM32_L4_PM_LPTIM_FREQ 32768
#define STM32_L4_PM_LPTIM_PER_TICK (STM32_L4_PM_LPTIM_FREQ / CONFIG_FREERTOS_TICK_RATE_HZ)

typedef enum {
	STM32_L4_PM_RET_OK = 0,
	STM32_L4_PM_RET_FAILED,
} stm32_l4_pm_ret_t;


typedef struct {
	volatile bool pm_ok;
	Pm pm;

	WakeLockGroup sleep_wlg;
} Stm32L4Pm;


clock_t stm32_l4_pm_clock(Stm32L4Pm *self);
stm32_l4_pm_ret_t stm32_l4_pm_init(Stm32L4Pm *self);
stm32_l4_pm_ret_t stm32_l4_pm_free(Stm32L4Pm *self);
stm32_l4_pm_ret_t stm32_l4_pm_lptimer_enable(void);

