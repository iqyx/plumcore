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
 * Transmit helpers
 *********************************************************************************************************************/

/* Queue as many bytes as fit into the transmit ring buffer and start the transmission. TCIE stays enabled until the
 * transmission completes to release the driver enable and wake up a writer waiting for the completion. */
static size_t tx_queue(Stm32Uart *self, const uint8_t *buf, size_t size) {
	USART_TypeDef *port = (USART_TypeDef *)self->port;

	size_t written = 0;
	ringbuf_write(&self->txring, buf, size, &written);
	if (written > 0) {
		/* Assert driver enable if set. */
		if (self->de_gpio) {
			self->de_gpio->vmt->set(self->de_gpio, true);
		}

		/* The ISR clears these bits too, do not race with it. */
		taskENTER_CRITICAL();
		port->CR1 |= USART_CR1_TXEIE_TXFNFIE | USART_CR1_TCIE;
		taskEXIT_CRITICAL();
	}
	return written;
}


/* Wait until all the queued data is transmitted, including the last frame. The ISR leaves the TC flag set, it is
 * cleared only by writing the next data. Called with txmutex held. */
static void tx_wait_complete(Stm32Uart *self) {
	USART_TypeDef *port = (USART_TypeDef *)self->port;

	while (true) {
		size_t used = 0;
		ringbuf_used(&self->txring, &used);
		if (used == 0 && (port->ISR & USART_ISR_TC)) {
			return;
		}
		taskENTER_CRITICAL();
		port->CR1 |= USART_CR1_TCIE;
		taskEXIT_CRITICAL();
		xSemaphoreTake(self->txsem, portMAX_DELAY);
	}
}


/* Duration of @p frames frames (10 bits each, 8N1) at the current bitrate in core clock cycles. */
static uint32_t tx_frame_cycles(Stm32Uart *self, uint32_t frames) {
	uint32_t bit_cycles = (SystemCoreClock + self->bitrate - 1) / self->bitrate;
	return frames * 10 * bit_cycles;
}


/* Wait for @p cycles core clock cycles. Sleep if it takes at least two ticks, busy wait on the cycle counter
 * otherwise. */
static void tx_wait_cycles(uint32_t cycles) {
	uint32_t tick_cycles = SystemCoreClock / configTICK_RATE_HZ;

	if (cycles >= 2 * tick_cycles) {
		/* Round up, the current tick is already partially elapsed. */
		vTaskDelay(cycles / tick_cycles + 1);
		return;
	}

	uint32_t start = DWT->CYCCNT;
	while ((DWT->CYCCNT - start) < cycles) {
		;
	}
}


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
	 * reconfiguration and restore them once the port is re-enabled. The TC flag is left set, the ISR disables TCIE
	 * when it handles it. */
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
	stm32_uart->bitrate = bitrate_baud;

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


static uart_ret_t uart_send_break(Uart *self, uint32_t frames) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;
	USART_TypeDef *port = (USART_TypeDef *)stm32_uart->port;

	xSemaphoreTake(stm32_uart->txmutex, portMAX_DELAY);
	tx_wait_complete(stm32_uart);

	if (stm32_uart->de_gpio) {
		stm32_uart->de_gpio->vmt->set(stm32_uart->de_gpio, true);
	}
	for (uint32_t i = 0; i < frames; i++) {
		/* The hardware sends a single break character per request and signals its end only by clearing SBKF
		 * during the stop bits, without an interrupt. Wait for the break duration, then poll the rest. */
		port->RQR = USART_RQR_SBKRQ;
		tx_wait_cycles(tx_frame_cycles(stm32_uart, 1));
		while (port->ISR & USART_ISR_SBKF) {
			;
		}
	}
	if (stm32_uart->de_gpio) {
		stm32_uart->de_gpio->vmt->set(stm32_uart->de_gpio, false);
	}

	xSemaphoreGive(stm32_uart->txmutex);
	return UART_RET_OK;
}


static uart_ret_t uart_send_idle(Uart *self, uint32_t frames) {
	if (u_assert(self != NULL)) {
		return UART_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;
	USART_TypeDef *port = (USART_TypeDef *)stm32_uart->port;

	/* The hardware can send only a single idle frame by toggling TE and does not signal its end. Time the idle
	 * period in software after the previous data has been completely transmitted instead. */
	xSemaphoreTake(stm32_uart->txmutex, portMAX_DELAY);
	tx_wait_complete(stm32_uart);

	/* Sense the line with the receiver. Any reception (including the echo of our own transmission on a shared
	 * medium) restarts the idle period, so the transmission does not collide with another node. While the line
	 * is busy, sleep until the receiver timeout signals its end. Without the receiver timeout nobody gives the
	 * semaphore and the line is checked every tick. The timeout also recovers from a start bit not followed by a
	 * whole character, which never raises the receiver timeout. */
	uint32_t cycles = tx_frame_cycles(stm32_uart, frames);
	while (true) {
		if ((port->ISR & USART_ISR_BUSY) || (stm32_uart->enable_rto && stm32_uart->rx_active)) {
			xSemaphoreTake(stm32_uart->idlesem, 1);
			continue;
		}
		uint32_t idle = DWT->CYCCNT - stm32_uart->rx_cycles;
		if (idle >= cycles) {
			break;
		}
		tx_wait_cycles(cycles - idle);
	}
	xSemaphoreGive(stm32_uart->txmutex);

	return UART_RET_OK;
}


static const struct uart_vmt uart_vmt = {
	.set_bitrate = uart_set_bitrate,
	.set_databits = uart_set_databits,
	.set_stopbits = uart_set_stopbits,
	.set_parity = uart_set_parity,
	.send_break = uart_send_break,
	.send_idle = uart_send_idle,
};


/*********************************************************************************************************************
 * Stream interface implementation
 *********************************************************************************************************************/

static stream_ret_t stream_write(Stream *self, const void *buf, size_t size) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL)) {
		return STREAM_RET_FAILED;
	}
	Stm32Uart *stm32_uart = (Stm32Uart *)self->parent;

	xSemaphoreTake(stm32_uart->txmutex, portMAX_DELAY);
	/* Write the whole buffer, wait for the ISR to free some space if it does not fit. */
	while (size > 0) {
		size_t written = tx_queue(stm32_uart, buf, size);
		buf = (const uint8_t *)buf + written;
		size -= written;
		if (size > 0) {
			xSemaphoreTake(stm32_uart->txsem, portMAX_DELAY);
		}
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

	TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
	TimeOut_t timeout;
	vTaskSetTimeOutState(&timeout);

	if (xSemaphoreTake(stm32_uart->txmutex, ticks) == pdFALSE) {
		/* We were not even able to start. */
		return STREAM_RET_TIMEOUT;
	}

	/* We are allowed to write less bytes than requested. Write what fits, wait for some space if nothing does. */
	size_t w = 0;
	while ((w = tx_queue(stm32_uart, buf, size)) == 0) {
		if (xTaskCheckForTimeOut(&timeout, &ticks) == pdTRUE ||
		    xSemaphoreTake(stm32_uart->txsem, ticks) != pdTRUE) {
			xSemaphoreGive(stm32_uart->txmutex);
			return STREAM_RET_TIMEOUT;
		}
	}

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

	/* The cycle counter times short idle and break periods. */
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	#if defined(STM32H7)
		DWT->LAR = 0xc5acce55;
	#endif
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	/* Allocate IPC primitives. */
	ringbuf_init(&self->rxring, self->rxring_data, sizeof(self->rxring_data));
	self->rxsem = xSemaphoreCreateBinary();
	self->idlesem = xSemaphoreCreateBinary();
	ringbuf_init(&self->txring, self->txring_data, sizeof(self->txring_data));
	self->txsem = xSemaphoreCreateBinary();
	if (self->rxsem == NULL || self->idlesem == NULL || self->txsem == NULL) {
		goto err;
	}
	self->txmutex = xSemaphoreCreateMutex();
	if (self->txmutex == NULL) {
		goto err;
	}


	/* Enable RX not empty interrupt to receive data. */
	port->CR3 |= USART_CR3_OVRDIS;
	port->CR1 |= USART_CR1_RXNEIE;

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

	if (self->txsem != NULL) {
		vSemaphoreDelete(self->txsem);
	}
	if (self->rxsem != NULL) {
		vSemaphoreDelete(self->rxsem);
	}
	if (self->idlesem != NULL) {
		vSemaphoreDelete(self->idlesem);
	}
	if (self->txmutex != NULL) {
		vSemaphoreDelete(self->txmutex);
	}

	return STM32_UART_RET_OK;
}


stm32_uart_ret_t stm32_uart_interrupt_handler(Stm32Uart *self) {
	USART_TypeDef *port = (USART_TypeDef *)self->port;
	BaseType_t woken = pdFALSE;

	/* Wake up the writer when space is freed or the transmission completes. */
	bool tx_event = false;
	while ((port->ISR & USART_ISR_TXE_TXFNF)) {
		uint8_t b = 0;
		if (ringbuf_get(&self->txring, &b) != RINGBUF_RET_OK) {
			/* No more bytes to send. Disable the interrupt. */
			port->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
			break;
		}
		port->TDR = (b & USART_TDR_TDR_Msk);
		tx_event = true;
	}

	/* Writing TDR above clears TC, it is set only after the last frame is transmitted. Leave the flag set as an
	 * indication of the completed transmission, disable its interrupt instead. */
	if ((port->CR1 & USART_CR1_TCIE) && (port->ISR & USART_ISR_TC)) {
		/* De-assert driver enable if set. */
		if (self->de_gpio) {
			self->de_gpio->vmt->set(self->de_gpio, false);
		}

		port->CR1 &= ~USART_CR1_TCIE;
		tx_event = true;
	}
	if (tx_event) {
		xSemaphoreGiveFromISR(self->txsem, &woken);
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
	bool data = false;
	while (port->ISR & USART_ISR_RXNE_RXFNE) {
		uint8_t c = (uint8_t)(port->RDR & USART_RDR_RDR_Msk);
		data = true;
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
	if (data) {
		/* Only the data marks the line activity, not the gap marker. */
		self->rx_cycles = DWT->CYCCNT;
		self->rx_active = true;
	}
	if (received) {
		xSemaphoreGiveFromISR(self->rxsem, &woken);
	}
	if (rto) {
		/* The bytes read above belong to the frame which ended, the line is idle now. */
		self->rx_active = false;
		xSemaphoreGiveFromISR(self->idlesem, &woken);
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

