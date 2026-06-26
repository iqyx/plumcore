/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 1-Wire bus master over a UART peripheral
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <main.h>

#include <interfaces/uart.h>
#include <interfaces/stream.h>
#include <interfaces/ow.h>

#include "uart-ow.h"

#define MODULE_NAME "uart-ow"


/* 1-Wire-over-UART bitrates: the reset/presence slot is generated at 9600 baud (the framing start bit plus the low
 * data bits of 0xF0 form the ~520 us reset low pulse) while each bit slot is generated at 115200 baud (one UART byte
 * per 1-Wire bit). */
#define UART_OW_RESET_BAUD 9600
#define UART_OW_DATA_BAUD 115200

/* Stream read timeout for the echoed bytes of a single transfer. Generous compared to the on-wire time. */
#define UART_OW_IO_TIMEOUT_MS 20

/* Largest 1-Wire transfer performed in a single UART burst (in 1-Wire bytes). Each 1-Wire byte expands to 8 UART
 * bytes, so this stays well within the UART rx buffer. */
#define UART_OW_MAX_BYTES 16

/* ROM commands. */
#define UART_OW_ROM_SKIP 0xcc


/*********************************************************************************************************************
 * 1-Wire link layer over the UART
 *********************************************************************************************************************/

/* Discard any stale bytes left in the UART receive buffer before starting a new transfer. */
static void ow_flush(UartOw *self) {
	uint8_t tmp[16];
	size_t read = 0;
	while (self->stream->vmt->read_timeout(self->stream, tmp, sizeof(tmp), &read, 0) == STREAM_RET_OK) {
		;
	}
}


/* Read exactly @p len bytes from the stream, looping until they all arrive or a timeout occurs. */
static ow_ret_t ow_read_exact(UartOw *self, uint8_t *buf, size_t len) {
	size_t got = 0;
	while (got < len) {
		size_t read = 0;
		if (self->stream->vmt->read_timeout(self->stream, buf + got, len - got, &read,
		                                    UART_OW_IO_TIMEOUT_MS) != STREAM_RET_OK) {
			return OW_RET_FAILED;
		}
		got += read;
	}
	return OW_RET_OK;
}


/*********************************************************************************************************************
 * Ow interface implementation
 *********************************************************************************************************************/

/*
 * Transfer @p n 1-Wire bytes in a single UART burst. Each 1-Wire bit is encoded as one UART byte: 0xFF drives a
 * short low pulse (write/read '1') and 0x00 drives a long low pulse (write '0'). On a single-wire half-duplex UART
 * the line state is read back for every transmitted byte, so the device response is recovered by sampling the echo:
 * a returned 0xFF means the line stayed high (bit '1'), anything else means the device pulled it low (bit '0').
 *
 * If @p out is NULL all bit slots are read slots (0xFF). If @p in is NULL the read-back bytes are discarded.
 */
static ow_ret_t uart_ow_exchange(Ow *ow, const uint8_t *out, uint8_t *in, size_t n) {
	UartOw *self = ow->parent;
	if (n == 0 || n > UART_OW_MAX_BYTES) {
		return OW_RET_FAILED;
	}

	uint8_t tx[UART_OW_MAX_BYTES * 8];
	uint8_t rx[UART_OW_MAX_BYTES * 8];

	for (size_t i = 0; i < n; i++) {
		uint8_t b = (out != NULL) ? out[i] : 0xff;
		for (size_t bit = 0; bit < 8; bit++) {
			tx[i * 8 + bit] = (b & (1 << bit)) ? 0xff : 0x00;
		}
	}

	ow_flush(self);
	if (self->stream->vmt->write(self->stream, tx, n * 8) != STREAM_RET_OK) {
		return OW_RET_FAILED;
	}
	if (ow_read_exact(self, rx, n * 8) != OW_RET_OK) {
		return OW_RET_FAILED;
	}

	if (in != NULL) {
		for (size_t i = 0; i < n; i++) {
			uint8_t b = 0;
			for (size_t bit = 0; bit < 8; bit++) {
				if (rx[i * 8 + bit] == 0xff) {
					b |= (1 << bit);
				}
			}
			in[i] = b;
		}
	}
	return OW_RET_OK;
}


/* Generate a 1-Wire bus reset and detect the device presence pulse. */
static ow_ret_t uart_ow_reset(Ow *ow, bool *present) {
	UartOw *self = ow->parent;
	self->uart->vmt->set_bitrate(self->uart, UART_OW_RESET_BAUD);
	ow_flush(self);

	uint8_t tx = 0xf0;
	uint8_t rx = 0xf0;
	ow_ret_t ret = OW_RET_OK;
	if (self->stream->vmt->write(self->stream, &tx, 1) != STREAM_RET_OK ||
	    ow_read_exact(self, &rx, 1) != OW_RET_OK) {
		ret = OW_RET_FAILED;
	}

	self->uart->vmt->set_bitrate(self->uart, UART_OW_DATA_BAUD);
	if (ret != OW_RET_OK) {
		return ret;
	}

	/* Without a device the line follows our transmission and reads back as 0xF0; a present device pulls the line
	 * low during the presence window, corrupting the echoed byte. */
	if (present != NULL) {
		*present = (rx != 0xf0);
	}
	return OW_RET_OK;
}


/* Reset the bus and address the (single) device on it using SKIP ROM. */
static ow_ret_t uart_ow_select(Ow *ow) {
	bool present = false;
	if (uart_ow_reset(ow, &present) != OW_RET_OK || !present) {
		return OW_RET_FAILED;
	}
	uint8_t cmd = UART_OW_ROM_SKIP;
	return uart_ow_exchange(ow, &cmd, NULL, 1);
}


static const struct ow_vmt uart_ow_vmt = {
	.exchange = uart_ow_exchange,
	.reset = uart_ow_reset,
	.select = uart_ow_select,
};


/*********************************************************************************************************************
 * Public API
 *********************************************************************************************************************/

ow_ret_t uart_ow_init(UartOw *self, Uart *uart, Stream *stream) {
	if (u_assert(self != NULL) ||
	    u_assert(uart != NULL) ||
	    u_assert(stream != NULL)) {
		return OW_RET_FAILED;
	}
	memset(self, 0, sizeof(UartOw));
	self->uart = uart;
	self->stream = stream;

	self->ow.parent = self;
	self->ow.vmt = &uart_ow_vmt;

	self->uart->vmt->set_bitrate(self->uart, UART_OW_DATA_BAUD);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return OW_RET_OK;
}


ow_ret_t uart_ow_free(UartOw *self) {
	if (u_assert(self != NULL)) {
		return OW_RET_FAILED;
	}
	return OW_RET_OK;
}


ow_ret_t uart_ow_get_ow(UartOw *self, Ow **ow) {
	if (u_assert(self != NULL) ||
	    u_assert(ow != NULL)) {
		return OW_RET_FAILED;
	}
	*ow = &self->ow;
	return OW_RET_OK;
}
