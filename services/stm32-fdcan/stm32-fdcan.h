/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 FDCAN interface driver
 *
 * Copyright (c) 2016-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <main.h>
#include <interfaces/can.h>


typedef enum {
	STM32_FDCAN_RET_OK = 0,
	STM32_FDCAN_RET_FAILED,
} stm32_fdcan_ret_t;


typedef struct {
	Can iface;

	/** @brief Base address of the FDCAN peripheral (libopencm3 canport handle). */
	uint32_t fdcan;
	/** @brief Signalled from the IRQ handler whenever a frame arrives in RX FIFO 0. */
	SemaphoreHandle_t rx_sem;
} Stm32Fdcan;


/**
 * @brief Initialise the FDCAN interface driver
 *
 * The caller is responsible for configuring the FDCAN pins, enabling the
 * peripheral clock and setting up the bit timing (e.g. using the libopencm3
 * fdcan_init()/fdcan_set_can()/fdcan_start() sequence) prior to calling this
 * function. This only wires the already-running peripheral to the @ref Can
 * interface and prepares the receive synchronisation.
 *
 * @param self FDCAN driver instance
 * @param fdcan Base address of the FDCAN peripheral (libopencm3 canport handle)
 *
 * @return STM32_FDCAN_RET_OK on success, STM32_FDCAN_RET_FAILED otherwise.
 */
stm32_fdcan_ret_t stm32_fdcan_init(Stm32Fdcan *self, uint32_t fdcan);
stm32_fdcan_ret_t stm32_fdcan_free(Stm32Fdcan *self);

/**
 * @brief Handle a FDCAN peripheral interrupt
 *
 * Call from the corresponding FDCANx_IT0 interrupt service routine. It clears
 * the handled interrupt flags and wakes up any thread waiting for a received frame.
 *
 * @param self FDCAN driver instance
 *
 * @return STM32_FDCAN_RET_OK on success.
 */
stm32_fdcan_ret_t stm32_fdcan_irq_handler(Stm32Fdcan *self);
