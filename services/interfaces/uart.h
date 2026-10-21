/* SPDX-License-Identifier: BSD-2-Clause
 *
 * UART interface
 *
 * Copyright (c) 2018-2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>


typedef enum uart_ret {
	UART_RET_OK = 0,
	UART_RET_FAILED,
} uart_ret_t;

enum uart_stopbits {
	UART_STOPBITS_1 = 0,
	UART_STOPBITS_1_5,
	UART_STOPBITS_2,
};

enum uart_parity {
	UART_PARITY_NONE = 0,
	UART_PARITY_ODD,
	UART_PARITY_EVEN,
};

typedef struct uart Uart;
struct uart_vmt {
	uart_ret_t (*set_bitrate)(Uart *self, uint32_t bitrate_baud);
	uart_ret_t (*set_databits)(Uart *self, uint32_t b);
	uart_ret_t (*set_stopbits)(Uart *self, enum uart_stopbits b);
	uart_ret_t (*set_parity)(Uart *self, enum uart_parity p);

	/* Send @p frames break characters after all the data written before has been transmitted. Returns once the
	 * breaks are sent. The length is in frames (bytes with 8N1). */
	uart_ret_t (*send_break)(Uart *self, uint32_t frames);

	/* Wait until the line stays idle for @p frames frame times after all the data written before has been
	 * transmitted. If the implementation can sense the line (e.g. on a shared half-duplex medium), any activity
	 * of other nodes restarts the idle period. Returns once the idle period elapsed, data written afterwards
	 * follows it. The length is in frames (bytes with 8N1). */
	uart_ret_t (*send_idle)(Uart *self, uint32_t frames);
};

typedef struct uart {
	const struct uart_vmt *vmt;
	void *parent;
} Uart;


