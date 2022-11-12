/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 UART driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>

#include "stm32-uart.h"

#define MODULE_NAME "stm32-uart"


/*********************************************************************************************************************
 * Uart interface implementation
 *********************************************************************************************************************/

/** @TODO wait for TX complete before reconfiguring */

static uart_ret_t uart_set_bitrate(Uart *self, uint32_t bitrate_baud) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;

	usart_disable(stm32_uart->port);
	usart_set_baudrate(stm32_uart->port, bitrate_baud);
	usart_enable(stm32_uart->port);

	return UART_RET_OK;
}


static uart_ret_t uart_set_databits(Uart *self, uint32_t b) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;

	usart_disable(stm32_uart->port);
	usart_set_databits(stm32_uart->port, b);
	usart_enable(stm32_uart->port);

	return UART_RET_OK;
}


static uart_ret_t uart_set_stopbits(Uart *self, enum uart_stopbits b) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;

	usart_disable(stm32_uart->port);
	switch (b) {
		case UART_STOPBITS_1:
			usart_set_stopbits(stm32_uart->port, USART_STOPBITS_1);
			break;
		case UART_STOPBITS_1_5:
			usart_set_stopbits(stm32_uart->port, USART_STOPBITS_1_5);
			break;
		case UART_STOPBITS_2:
			usart_set_stopbits(stm32_uart->port, USART_STOPBITS_2);
			break;
		default:
			usart_set_stopbits(stm32_uart->port, USART_STOPBITS_1);
	}
	usart_enable(stm32_uart->port);

	return UART_RET_OK;
}


static uart_ret_t uart_set_parity(Uart *self, enum uart_parity p) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;

	usart_disable(stm32_uart->port);
	switch (p) {
		case UART_PARITY_NONE:
			usart_set_parity(stm32_uart->port, USART_PARITY_NONE);
			break;
		case UART_PARITY_ODD:
			usart_set_parity(stm32_uart->port, USART_PARITY_ODD);
			break;
		case UART_PARITY_EVEN:
			usart_set_parity(stm32_uart->port, USART_PARITY_EVEN);
			break;
		default:
			usart_set_parity(stm32_uart->port, USART_PARITY_NONE);
			break;
	}
	usart_enable(stm32_uart->port);

	return UART_RET_OK;
}


static const struct uart_vmt uart_vmt = {
	.set_bitrate = uart_set_bitrate,
	.set_databits = uart_set_databits,
	.set_stopbits = uart_set_stopbits,
	.set_parity = uart_set_parity
};


/*********************************************************************************************************************
 * Stream interface implementation
 *********************************************************************************************************************/

static size_t min_size(size_t a, size_t b) {
	if (a < b) {
		return a;
	} else {
		return b;
	}
}


static stream_ret_t stream_write(Stream *self, const void *buf, size_t size) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL)) {
		return STREAM_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;

	/* FreeRTOS stream buffers allow only a single writer. This is not a problem here since
	 * we can wait forever. */
	xSemaphoreTake(stm32_uart->txmutex, portMAX_DELAY);
	/* Write the whole buffer. Pay attention to the maximum allowed size by FreeRTOS. */
	while (size > 0) {
		size_t to_write = min_size(CONFIG_SERVICE_STM32_UART_TXBUF_SIZE / 2, size);

		size_t written = xStreamBufferSend(stm32_uart->txbuf, buf, to_write, 0);
		/* And send the data. It is important TXEIE remains set. */
		USART_CR1(stm32_uart->port) |= USART_CR1_TXEIE;

		buf = (const uint8_t *)buf + written;
		size -= written;
	}
	xSemaphoreGive(stm32_uart->txmutex);
	return STREAM_RET_OK;
}


static stream_ret_t stream_read(Stream *self, void *buf, size_t size, size_t *read) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(size > 0)) {
		return STREAM_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;

	size_t r = xStreamBufferReceive(stm32_uart->rxbuf, buf, size, portMAX_DELAY);
	if (r == 0) {
		return STREAM_RET_EOF;
	} else if (r > 0) {
		if (read != NULL) {
			*read = r;
		}
		return STREAM_RET_OK;
	}

	return STREAM_RET_FAILED;
}


static stream_ret_t stream_write_timeout(Stream *self, const void *buf, size_t size, size_t *written, uint32_t timeout_ms) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(size > 0)) {
		return STREAM_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;

	/* We are allowed to write less bytes than requested. Crop the buffer. */
	size = min_size(CONFIG_SERVICE_STM32_UART_TXBUF_SIZE / 2, size);

	/* Mutex locking is somewhat complicated in this situation. We have to consider the timeout. */
	if (xSemaphoreTake(stm32_uart->txmutex, pdMS_TO_TICKS(timeout_ms)) == pdFALSE) {
		/* We were not even able to start. */
		return STREAM_RET_TIMEOUT;
	}
	size_t w = xStreamBufferSend(stm32_uart->txbuf, buf, size, pdMS_TO_TICKS(timeout_ms));
	if (w == 0) {
		xSemaphoreGive(stm32_uart->txmutex);
		return STREAM_RET_TIMEOUT;
	}
	/* Enable TX empty interrupt to send the data. */
	USART_CR1(stm32_uart->port) |= USART_CR1_TXEIE;

	if (written != NULL) {
		*written = w;
	}

	xSemaphoreGive(stm32_uart->txmutex);
	return STREAM_RET_OK;
}


static stream_ret_t stream_read_timeout(Stream *self, void *buf, size_t size, size_t *read, uint32_t timeout_ms) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(size > 0)) {
		return STREAM_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;

	size_t r = xStreamBufferReceive(stm32_uart->rxbuf, buf, size, pdMS_TO_TICKS(timeout_ms));
	if (r == 0) {
		return STREAM_RET_TIMEOUT;
	} else if (r > 0) {
		if (read != NULL) {
			*read = r;
		}
		return STREAM_RET_OK;
	}

	return STREAM_RET_FAILED;
}


static const struct stream_vmt stream_vmt = {
	.write = stream_write,
	.read = stream_read,
	.write_timeout = stream_write_timeout,
	.read_timeout = stream_read_timeout
};


/*********************************************************************************************************************/


stm32_uart_ret_t stm32_uart_init(Stm32Uart *self, uint32_t port) {
	if (u_assert(self != NULL)) {
		return STM32_UART_RET_FAILED;
	}

	self->port = port;

	/* Setup interfaces */
	self->stream.parent = self;
	self->stream.vmt = &stream_vmt;
	self->uart.parent = self;
	self->uart.vmt = &uart_vmt;

	/* Set default UART parameters. */
	usart_disable(self->port);
	usart_set_baudrate(self->port, 115200);
	usart_set_mode(self->port, USART_MODE_TX_RX);
	usart_set_databits(self->port, 8);
	usart_set_stopbits(self->port, USART_STOPBITS_1);
	usart_set_parity(self->port, USART_PARITY_NONE);
	usart_set_flow_control(self->port, USART_FLOWCONTROL_NONE);
	usart_enable(self->port);

	/* Allocate IPC primitives. */
	self->rxbuf = xStreamBufferCreate(CONFIG_SERVICE_STM32_UART_RXBUF_SIZE, 1);
	self->txbuf = xStreamBufferCreate(CONFIG_SERVICE_STM32_UART_TXBUF_SIZE, 1);
	if (self->rxbuf == NULL || self->txbuf == NULL) {
		goto err;
	}
	self->txmutex = xSemaphoreCreateMutex();
	if (self->txmutex == NULL) {
		goto err;
	}

	/* Enable RX not empty interrupt to receive data. */
	USART_CR3(self->port) |= USART_CR3_OVRDIS;
	USART_CR1(self->port) |= USART_CR1_RXNEIE;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("port %p initialized"), port);
	return STM32_UART_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("port %p init failed"), port);
	return STM32_UART_RET_FAILED;
}


stm32_uart_ret_t stm32_uart_free(Stm32Uart *self) {
	if (u_assert(self != NULL)) {
		return STM32_UART_RET_FAILED;
	}

	if (self->txbuf != NULL) {
		vStreamBufferDelete(self->txbuf);
	}
	if (self->rxbuf != NULL) {
		vStreamBufferDelete(self->rxbuf);
	}
	if (self->txmutex != NULL) {
		vSemaphoreDelete(self->txmutex);
	}

	return STM32_UART_RET_OK;
}


stm32_uart_ret_t stm32_uart_interrupt_handler(Stm32Uart *self) {
	/** @TODO tested on L4 only */

	if ((USART_ISR(self->port) & USART_ISR_TXE)) {
		uint8_t b = 0;
		size_t r = xStreamBufferReceiveFromISR(self->txbuf, &b, sizeof(b), 0);
		if (r > 0) {
			usart_send(self->port, b);
		}
		if (r == 0) {
			/* No more bytes to send. Clear and disable the interrupt. */
			USART_RQR(self->port) |= USART_RQR_TXFRQ;
			USART_CR1(self->port) &= ~USART_CR1_TXEIE;
		}
	}

	if ((USART_ISR(self->port) & USART_ISR_RXNE)) {
		uint8_t b = usart_recv(self->port);

		BaseType_t woken = pdFALSE;
		/* Do not check the return value. If there was not enough space to save
		 * the received byte, we have nothing else to do. */
		xStreamBufferSendFromISR(self->rxbuf, &b, sizeof(b), &woken);
		portYIELD_FROM_ISR(woken);
	}

	/* Enabling the RXNE interrupt also enables ORE. We must handle it properly. */
	if ((USART_ISR(self->port) & USART_ISR_ORE)) {
		USART_ICR(self->port) |= USART_ICR_ORECF;
	}

	return STM32_UART_RET_OK;
}

