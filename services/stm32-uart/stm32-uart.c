/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 UART driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Some code and computations borowwed from the libopencm3 project.
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>

#if defined(STM32G4)
	#include <stm32g4xx.h>
#elif defined(STM32H7)
	#include <stm32h7xx.h>
#elif defined(STM32U5)
	#include <stm32u5xx.h>
#else
	#error "stm32-uart service is not compatible with this MCU family"
#endif


#include "stm32-uart.h"

#define MODULE_NAME "stm32-uart"

#if !defined(USART_CR1_FIFOEN)
#define USART_CR1_FIFOEN (1 << 29)
#endif

/* The STM32U5 CMSIS headers name the data register field masks without the _Msk suffix. */
#if !defined(USART_TDR_TDR_Msk)
#define USART_TDR_TDR_Msk USART_TDR_TDR
#endif
#if !defined(USART_RDR_RDR_Msk)
#define USART_RDR_RDR_Msk USART_RDR_RDR
#endif

/* With the receiver timeout enabled, the ISR stores a received ESC byte as ESC ESC and marks the receiver timeout (a gap
 * in the received data) as ESC ETB in the receive ring buffer. */
#define STM32_UART_ESC 0x1b
#define STM32_UART_ETB 0x17


/*********************************************************************************************************************
 * Uart interface implementation
 *********************************************************************************************************************/

/** @TODO wait for TX complete before reconfiguring */

static uart_ret_t uart_set_bitrate(Uart *self, uint32_t bitrate_baud) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;
	USART_TypeDef *port = (USART_TypeDef *)stm32_uart->port;

	/* Disabling the USART (UE=0) below to reprogram BRR re-asserts the TC and TXE flags to their reset (active)
	 * state, and those flags cannot be cleared while UE=0. If their interrupt enables stayed set, the handler
	 * would spin on the uncleared flags and starve the caller, which is fatal when the bitrate is switched at
	 * runtime (e.g. a 1-Wire master toggling between reset and bit-slot rates). Mask them across the
	 * reconfiguration and restore them once the port is re-enabled and the flags can be cleared again. */
	uint32_t txie = port->CR1 & (USART_CR1_TCIE | USART_CR1_TXEIE_TXFNFIE);
	port->CR1 &= ~(USART_CR1_TCIE | USART_CR1_TXEIE_TXFNFIE);

	stm32_uart_enable(stm32_uart, false);
	/** @todo clock selection */
	uint32_t clock = SystemCoreClock;
	#if defined(USART_BRR_DIV_FRACTION)
		const uint32_t divider = ((2 * clock) + (bitrate_baud / 2)) / bitrate_baud;
		port->BRR = (divider & USART_BRR_DIV_MANTISSA_Msk) | ((divider & USART_BRR_DIV_FRACTION_Msk) >> 1U);
	#else
		port->BRR = (clock + bitrate_baud / 2) / bitrate_baud;
	#endif
	stm32_uart_enable(stm32_uart, true);

	port->ICR = USART_ICR_TCCF;
	port->CR1 |= txie;

	return UART_RET_OK;
}


static uart_ret_t uart_set_databits(Uart *self, uint32_t b) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;
	USART_TypeDef *port = (USART_TypeDef *)stm32_uart->port;

	stm32_uart_enable(stm32_uart, false);
	switch (b) {
		case 8:
			port->CR1 &= ~USART_CR1_M;
			break;
		case 9:
			port->CR1 = (port->CR1 & ~USART_CR1_M) | USART_CR1_M0;
			break;
		default:
			stm32_uart_enable(stm32_uart, true);
			return UART_RET_FAILED;
	}
	stm32_uart_enable(stm32_uart, true);

	return UART_RET_OK;
}


static uart_ret_t uart_set_stopbits(Uart *self, enum uart_stopbits b) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;
	USART_TypeDef *port = (USART_TypeDef *)stm32_uart->port;

	stm32_uart_enable(stm32_uart, false);
	switch (b) {
		case UART_STOPBITS_1:
		default:
			port->CR2 = (port->CR2 & ~USART_CR2_STOP_Msk);
			break;
		case UART_STOPBITS_1_5:
			port->CR2 = (port->CR2 & ~USART_CR2_STOP_Msk) | USART_CR2_STOP_0 | USART_CR2_STOP_1;
			break;
		case UART_STOPBITS_2:
			port->CR2 = (port->CR2 & ~USART_CR2_STOP_Msk) | USART_CR2_STOP_1;
			break;
	}
	stm32_uart_enable(stm32_uart, true);

	return UART_RET_OK;
}


static uart_ret_t uart_set_parity(Uart *self, enum uart_parity p) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;
	USART_TypeDef *port = (USART_TypeDef *)stm32_uart->port;

	stm32_uart_enable(stm32_uart, false);
	switch (p) {
		case UART_PARITY_NONE:
		default:
			port->CR1 &= ~USART_CR1_PS;
			port->CR1 &= ~USART_CR1_PCE;
			break;
		case UART_PARITY_ODD:
			port->CR1 |= USART_CR1_PS | USART_CR1_PCE;
			break;
		case UART_PARITY_EVEN:
			port->CR1 |= USART_CR1_PCE;
			break;
	}
	stm32_uart_enable(stm32_uart, true);

	return UART_RET_OK;
}


static const struct uart_vmt uart_vmt = {
	.set_bitrate = uart_set_bitrate,
	.set_databits = uart_set_databits,
	.set_stopbits = uart_set_stopbits,
	.set_parity = uart_set_parity,
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
	USART_TypeDef *port = (USART_TypeDef *)stm32_uart->port;

	/* FreeRTOS stream buffers allow only a single writer. This is not a problem here since
	 * we can wait forever. */
	xSemaphoreTake(stm32_uart->txmutex, portMAX_DELAY);
	/* Write the whole buffer. Pay attention to the maximum allowed size by FreeRTOS. */
	while (size > 0) {
		size_t to_write = min_size(CONFIG_SERVICE_STM32_UART_TXBUF_SIZE / 2, size);

		size_t written = xStreamBufferSend(stm32_uart->txbuf, buf, to_write, portMAX_DELAY);

		/* Assert driver enable if set. */
		if (stm32_uart->de_gpio) {
			stm32_uart->de_gpio->vmt->set(stm32_uart->de_gpio, true);
		}

		/* And send the data. It is important TXEIE remains set. */
		port->CR1 |= USART_CR1_TXEIE;

		buf = (const uint8_t *)buf + written;
		size -= written;
	}
	xSemaphoreGive(stm32_uart->txmutex);
	return STREAM_RET_OK;
}


static stream_ret_t stream_write_timeout(Stream *self, const void *buf, size_t size, size_t *written, uint32_t timeout_ms) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(size > 0)) {
		return STREAM_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;
	USART_TypeDef *port = (USART_TypeDef *)stm32_uart->port;

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

	/* Assert driver enable if set. */
	if (stm32_uart->de_gpio) {
		stm32_uart->de_gpio->vmt->set(stm32_uart->de_gpio, true);
	}

	/* Enable TX empty interrupt to send the data. */
	port->CR1 |= USART_CR1_TXEIE;

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

	/* Allow using constant for the max delay, see @p stream_read below. */
	TickType_t ticks = portMAX_DELAY;
	if (timeout_ms != portMAX_DELAY) {
		ticks = pdMS_TO_TICKS(timeout_ms);
	}
	TimeOut_t timeout;
	vTaskSetTimeOutState(&timeout);

	stream_ret_t ret = STREAM_RET_OK;
	size_t i = 0;
	while (i < size) {
		uint8_t c;
		if (ringbuf_get(&stm32_uart->rxring, &c) != RINGBUF_RET_OK) {
			/* Return what is available. Wait with timeout for the first byte only. */
			if (i > 0) {
				break;
			}
			if (xTaskCheckForTimeOut(&timeout, &ticks) == pdTRUE ||
			    xSemaphoreTake(stm32_uart->rxsem, ticks) != pdTRUE) {
				ret = STREAM_RET_TIMEOUT;
				break;
			}
			continue;
		}

		if (stm32_uart->enable_rto && c == STM32_UART_ESC) {
			/* The ISR stores the ESC together with the following byte, it is always available. */
			ringbuf_get(&stm32_uart->rxring, &c);
			if (c == STM32_UART_ETB) {
				/* A gap in the received data ends the read, even with no bytes read. */
				ret = STREAM_RET_EOT;
				break;
			}
		}
		((uint8_t *)buf)[i] = c;
		i++;
	}

	if (read != NULL) {
		*read = i;
	}
	return ret;
}


static stream_ret_t stream_read(Stream *self, void *buf, size_t size, size_t *read) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(size > 0)) {
		return STREAM_RET_FAILED;
	}

	return stream_read_timeout(self, buf, size, read, portMAX_DELAY);
}


static const struct stream_vmt stream_vmt = {
	.write = stream_write,
	.read = stream_read,
	.write_timeout = stream_write_timeout,
	.read_timeout = stream_read_timeout
};


/*********************************************************************************************************************/


stm32_uart_ret_t stm32_uart_init(Stm32Uart *self, void *port_base) {
	if (u_assert(self != NULL)) {
		return STM32_UART_RET_FAILED;
	}

	self->port = port_base;
	USART_TypeDef *port = (USART_TypeDef *)self->port;

	/* Setup interfaces */
	self->stream.parent = self;
	self->stream.vmt = &stream_vmt;
	self->uart.parent = self;
	self->uart.vmt = &uart_vmt;

	/* Set default UART parameters. FIFOEN can be written only while the USART is disabled. */
	stm32_uart_enable(self, false);
	port->CR1 |= USART_CR1_FIFOEN;
	uart_set_bitrate(&self->uart, 115200);
	port->CR1 |= USART_CR1_RE | USART_CR1_TE;
	uart_set_databits(&self->uart, 8);
	uart_set_stopbits(&self->uart, UART_STOPBITS_1);
	uart_set_parity(&self->uart, UART_PARITY_NONE);
	//usart_set_flow_control(self->port, USART_FLOWCONTROL_NONE);

	stm32_uart_enable(self, true);

	/* Allocate IPC primitives. */
	ringbuf_init(&self->rxring, self->rxring_data, sizeof(self->rxring_data));
	self->rxsem = xSemaphoreCreateBinary();
	self->txbuf = xStreamBufferCreate(CONFIG_SERVICE_STM32_UART_TXBUF_SIZE, 1);
	if (self->rxsem == NULL || self->txbuf == NULL) {
		goto err;
	}
	self->txmutex = xSemaphoreCreateMutex();
	if (self->txmutex == NULL) {
		goto err;
	}


	/* Enable RX not empty interrupt to receive data. */
	port->CR3 |= USART_CR3_OVRDIS;
	port->CR1 |= USART_CR1_RXNEIE;
	port->CR1 |= USART_CR1_TCIE;

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
	if (self->rxsem != NULL) {
		vSemaphoreDelete(self->rxsem);
	}
	if (self->txmutex != NULL) {
		vSemaphoreDelete(self->txmutex);
	}

	return STM32_UART_RET_OK;
}


stm32_uart_ret_t stm32_uart_interrupt_handler(Stm32Uart *self) {
	USART_TypeDef *port = (USART_TypeDef *)self->port;
	BaseType_t woken = pdFALSE;

	while ((port->ISR & USART_ISR_TXE_TXFNF)) {
		uint8_t b = 0;
		/** @todo 1 us! */
		size_t r = xStreamBufferReceiveFromISR(self->txbuf, &b, sizeof(b), 0);
		if (r > 0) {
			port->TDR = (b & USART_TDR_TDR_Msk);
		}
		if (r == 0) {
			/* No more bytes to send. Disable the interrupt. */
			port->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
			break;
		}
	}

	if (port->ISR & USART_ISR_TC) {
		/* De-assert driver enable if set. */
		if (self->de_gpio) {
			self->de_gpio->vmt->set(self->de_gpio, false);
		}

		port->ICR |= USART_ICR_TCCF;
	}

	/* Check the receiver timeout before draining the FIFO. If it is set, the line went idle after the last received
	 * byte, all the bytes still in the FIFO belong to the frame it ends. */
	bool rto = false;
	if (port->ISR & USART_ISR_RTOF) {
		port->ICR |= USART_ICR_RTOCF;
		rto = true;
	}

	/* Aggregate multiple receptions until the FIFO is empty. Escaped bytes and the gap marker are always stored
	 * together, never split between two ringbuf_put() calls. */
	uint8_t bbuf[32];
	size_t bbuf_len = 0;
	bool received = false;
	while (port->ISR & USART_ISR_RXNE_RXFNE) {
		uint8_t c = (uint8_t)(port->RDR & USART_RDR_RDR_Msk);
		if (self->enable_rto && c == STM32_UART_ESC) {
			bbuf[bbuf_len] = STM32_UART_ESC;
			bbuf_len++;
		}
		bbuf[bbuf_len] = c;
		bbuf_len++;
		if (bbuf_len > sizeof(bbuf) - 2) {
			ringbuf_put(&self->rxring, bbuf, bbuf_len);
			bbuf_len = 0;
			received = true;
		}
	}
	if (rto && self->enable_rto) {
		bbuf[bbuf_len] = STM32_UART_ESC;
		bbuf[bbuf_len + 1] = STM32_UART_ETB;
		bbuf_len += 2;
	}
	if (bbuf_len > 0) {
		ringbuf_put(&self->rxring, bbuf, bbuf_len);
		received = true;
	}
	if (received) {
		xSemaphoreGiveFromISR(self->rxsem, &woken);
	}

	/* Enabling the RXNE interrupt also enables ORE. We must handle it properly. */
	if ((port->ISR & USART_ISR_ORE)) {
		port->ICR |= USART_ICR_ORECF;
	}

	portYIELD_FROM_ISR(woken);
	return STM32_UART_RET_OK;
}


stm32_uart_ret_t stm32_uart_set_de(Stm32Uart *self, Gpio *de_gpio) {
	self->de_gpio = de_gpio;

	return STM32_UART_RET_OK;
}


stm32_uart_ret_t stm32_uart_set_rto(Stm32Uart *self, uint32_t chars) {
	USART_TypeDef *port = (USART_TypeDef *)self->port;

	stm32_uart_enable(self, false);
	self->enable_rto = (chars > 0);
	if (chars > 0) {
		/* The timeout is counted in bit periods, a character is 10 bits long (8N1). */
		port->CR2 |= USART_CR2_RTOEN;
		port->RTOR = (chars * 10) & USART_RTOR_RTO_Msk;

		/* Enable timeout interrupt. */
		port->CR1 |= USART_CR1_RTOIE;
	} else {
		port->CR2 &= ~USART_CR2_RTOEN;
		port->CR1 &= ~USART_CR1_RTOIE;
	}
	stm32_uart_enable(self, true);

	return STM32_UART_RET_OK;
}


stm32_uart_ret_t stm32_uart_set_swmode(Stm32Uart *self) {
	USART_TypeDef *port = (USART_TypeDef *)self->port;

	stm32_uart_enable(self, false);
	port->CR3 |= USART_CR3_HDSEL;
	stm32_uart_enable(self, true);

	return STM32_UART_RET_OK;
}


stm32_uart_ret_t stm32_uart_set_rxtx_swap(Stm32Uart *self, bool swap) {
	USART_TypeDef *port = (USART_TypeDef *)self->port;

	stm32_uart_enable(self, false);
	if (swap) {
		port->CR2 |= USART_CR2_SWAP;
	} else {
		port->CR2 &= ~USART_CR2_SWAP;
	}
	stm32_uart_enable(self, true);

	return STM32_UART_RET_OK;
}

stm32_uart_ret_t stm32_uart_set_tx_invert(Stm32Uart *self, bool invert) {
	USART_TypeDef *port = (USART_TypeDef *)self->port;

	stm32_uart_enable(self, false);
	if (invert) {
		port->CR2 |= USART_CR2_TXINV;
	} else {
		port->CR2 &= ~USART_CR2_TXINV;
	}
	stm32_uart_enable(self, true);

	return STM32_UART_RET_OK;
}


stm32_uart_ret_t stm32_uart_enable(Stm32Uart *self, bool enable) {
	USART_TypeDef *port = (USART_TypeDef *)self->port;

	if (enable) {
		port->CR1 |= USART_CR1_UE;
	} else {
		port->CR1 &= ~USART_CR1_UE;
	}

	return STM32_UART_RET_OK;
}

