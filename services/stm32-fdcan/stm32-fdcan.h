/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 FDCAN interface driver
 *
 * Copyright (c) 2016-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <main.h>
#include <interfaces/can.h>


typedef enum {
	STM32_FDCAN_RET_OK = 0,
	STM32_FDCAN_RET_FAILED = -1,
} stm32_fdcan_ret_t;


typedef struct {
	Can iface;

	uint32_t fdcan;
	SemaphoreHandle_t rx_sem;

} Stm32Fdcan;


stm32_fdcan_ret_t stm32_fdcan_init(Stm32Fdcan *self, uint32_t fdcan);
stm32_fdcan_ret_t stm32_fdcan_free(Stm32Fdcan *self);
stm32_fdcan_ret_t stm32_fdcan_irq_handler(Stm32Fdcan *self);

