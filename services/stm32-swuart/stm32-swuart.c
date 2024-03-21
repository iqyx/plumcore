/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 single wire UART bus driver
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/datagram.h>
#include <libopencm3/stm32/usart.h>

#include "stm32-swuart.h"

#define MODULE_NAME "stm32-swuart"


/** @todo use the Datagram interface instead. */

stm32_swuart_ret_t stm32_swuart_init(Stm32Swuart *self, uint32_t uart, uint32_t baudrate) {
	memset(self, 0, sizeof(Stm32Swuart));
	self->uart = uart;

	usart_disable(uart);
	usart_set_baudrate(uart, baudrate);
	usart_set_databits(uart, 8);
	usart_set_stopbits(uart, USART_STOPBITS_1);
	usart_set_parity(uart, USART_PARITY_NONE);
	usart_set_flow_control(uart, USART_FLOWCONTROL_NONE);
	/* Set receiver timeout enable. 3 character time. */
	USART_CR2(uart) |= USART_CR2_RTOEN;
	USART_RTOR(uart) = 20L;
	/* half duplex single wire mode. */
	USART_CR3(uart) |= USART_CR3_HDSEL;
	USART_CR3(uart) |= USART_CR3_OVRDIS;
	usart_enable(uart);

	return STM32_SWUART_RET_OK;
}


stm32_swuart_ret_t stm32_swuart_free(Stm32Swuart *self) {
	usart_disable(self->uart);
	/* Nothing to free. */

	return STM32_SWUART_RET_OK;
}


stm32_swuart_ret_t stm32_swuart_timeout(Stm32Swuart *self, uint32_t timeout) {
	USART_RTOR(self->uart) = timeout & 0xffffffL;

	return STM32_SWUART_RET_OK;
}


stm32_swuart_ret_t stm32_swuart_send_frame(Stm32Swuart *self, const uint8_t *buf, size_t len) {
	USART_CR1(self->uart) |= USART_CR1_TE;
	/* USART_RQR(self->uart) |= USART_RQR_SBKRQ; */
	while (len) {
		while ((USART_ISR(self->uart) & USART_ISR_TXE) == 0) {
			;
		}
		USART_TDR(self->uart) = *buf;
		buf++;
		len--;
	}
	while (!(USART_ISR(self->uart) & USART_ISR_TC)) {
		;
	}
	USART_CR1(self->uart) &= ~USART_CR1_TE;
	return STM32_SWUART_RET_OK;
}


stm32_swuart_ret_t stm32_swuart_recv_frame(Stm32Swuart *self, uint8_t *buf, size_t size, size_t *len) {
	USART_CR1(self->uart) |= USART_CR1_RE;
	*len = 0;
	while (true) {
		/* Received frame is too long. When the buf size is 0,
		 * the first comparison fails and returns immediately. */
		if (*len >= size) {
			return STM32_SWUART_RET_FAILED;
		}
		/* Now we have a space for at least one char in buf, */
		for (;;) {
			if (USART_ISR(self->uart) & USART_ISR_RTOF) {
				/* Receive timeout, end of frame. */
				USART_ICR(self->uart) |= USART_ICR_RTOCF;
				USART_CR1(self->uart) &= ~USART_CR1_RE;
				return STM32_SWUART_RET_OK;
			}
			if (USART_ISR(self->uart) & USART_ISR_RXNE) {
				break;
			}
		}
		*buf = USART_RDR(self->uart);
		/* Advance to the next byte. */
		buf++;
		(*len)++;
	}
	USART_CR1(self->uart) &= ~USART_CR1_RE;
	return STM32_SWUART_RET_OK;
}
