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

	/* Bytes to transmit, written by a single task holding txmutex and read by the ISR. The semaphore wakes up the
	 * writer when the ISR frees space or the transmission completes. */
	struct ringbuf txring;
	uint8_t txring_data[CONFIG_SERVICE_STM32_UART_TXBUF_SIZE];
	SemaphoreHandle_t txsem;
	SemaphoreHandle_t txmutex;

	/* Current bitrate, used to time the idle and break periods. */
	uint32_t bitrate;

	/* Cycle counter value of the last reception, the line is sensed idle since then. With the receiver timeout
	 * enabled, rx_active is set from a reception until the receiver timeout, which also gives idlesem to wake up
	 * a writer waiting for the line to become idle. */
	volatile uint32_t rx_cycles;
	volatile bool rx_active;
	SemaphoreHandle_t idlesem;
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
