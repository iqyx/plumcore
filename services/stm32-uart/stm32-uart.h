/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 UART driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/uart.h>
#include <interfaces/stream.h>


typedef enum stm32_uart_ret {
	STM32_UART_RET_OK = 0,
	STM32_UART_RET_FAILED,
} stm32_uart_ret_t;

typedef struct {
	Uart uart;
	Stream stream;
	uint32_t port;

	StreamBufferHandle_t rxbuf;
	StreamBufferHandle_t txbuf;
	SemaphoreHandle_t txmutex;
	volatile size_t pending_tx;
} Stm32Uart;


stm32_uart_ret_t stm32_uart_init(Stm32Uart *self, uint32_t port);
stm32_uart_ret_t stm32_uart_free(Stm32Uart *self);
stm32_uart_ret_t stm32_uart_interrupt_handler(Stm32Uart *self);
