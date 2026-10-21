/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 UART driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/gpio.h>
#include <interfaces/uart.h>
#include <interfaces/stream.h>

#include "ringbuf.h"


typedef enum stm32_uart_ret {
	STM32_UART_RET_OK = 0,
	STM32_UART_RET_FAILED,
} stm32_uart_ret_t;

typedef struct {
	Uart uart;
	Stream stream;
	void *port;
	bool enable_rto;

	Gpio *de_gpio;

	/* Received bytes, written by the ISR and read by a single task. With the receiver timeout enabled, the ISR
	 * escapes the data and marks the gaps between frames in-band (see stm32-uart.c). The semaphore wakes up a
	 * reader waiting for more data. */
	struct ringbuf rxring;
	uint8_t rxring_data[CONFIG_SERVICE_STM32_UART_RXBUF_SIZE];
	SemaphoreHandle_t rxsem;

	StreamBufferHandle_t txbuf;
	SemaphoreHandle_t txmutex;
	volatile size_t pending_tx;
} Stm32Uart;


stm32_uart_ret_t stm32_uart_init(Stm32Uart *self, void *port_base);
stm32_uart_ret_t stm32_uart_free(Stm32Uart *self);
stm32_uart_ret_t stm32_uart_interrupt_handler(Stm32Uart *self);
stm32_uart_ret_t stm32_uart_set_de(Stm32Uart *self, Gpio *de_gpio);

/* Enable the receiver timeout after @p chars idle character times, 0 disables it. With the receiver timeout enabled,
 * a stream read ends with STREAM_RET_EOT at each gap in the received data. */
stm32_uart_ret_t stm32_uart_set_rto(Stm32Uart *self, uint32_t chars);

stm32_uart_ret_t stm32_uart_set_swmode(Stm32Uart *self);
stm32_uart_ret_t stm32_uart_set_rxtx_swap(Stm32Uart *self, bool swap);
stm32_uart_ret_t stm32_uart_set_tx_invert(Stm32Uart *self, bool invert);
stm32_uart_ret_t stm32_uart_enable(Stm32Uart *self, bool enable);
