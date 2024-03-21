/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 single wire UART bus driver
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

typedef enum {
	STM32_SWUART_RET_OK,
	STM32_SWUART_RET_FAILED,

} stm32_swuart_ret_t;


typedef struct {
	uint32_t uart;

} Stm32Swuart;

stm32_swuart_ret_t stm32_swuart_init(Stm32Swuart *self, uint32_t uart, uint32_t baudrate);
stm32_swuart_ret_t stm32_swuart_free(Stm32Swuart *self);
stm32_swuart_ret_t stm32_swuart_timeout(Stm32Swuart *self, uint32_t timeout);
stm32_swuart_ret_t stm32_swuart_send_frame(Stm32Swuart *self, const uint8_t *buf, size_t len);
stm32_swuart_ret_t stm32_swuart_recv_frame(Stm32Swuart *self, uint8_t *buf, size_t size, size_t *len);
