/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Power manager for the STM32L4 MCU family
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <main.h>

#include "stm32-l4-pm.h"

#define MODULE_NAME "stm32-l4-pm"

/* The global singleton instance used to handle service IRQs. */
extern Stm32L4Pm STM32_L4_PM_LPTIM_IRQ_DELEGATE;
const Stm32L4Pm *irq_delegate = &STM32_L4_PM_LPTIM_IRQ_DELEGATE;
