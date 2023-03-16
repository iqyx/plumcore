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


typedef enum {
	STM32_L4_PM_RET_OK = 0,
	STM32_L4_PM_RET_FAILED,
} stm32_l4_pm_ret_t;


typedef struct {

} Stm32L4Pm;


