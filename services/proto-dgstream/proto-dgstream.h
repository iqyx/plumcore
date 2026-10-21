/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Framing of datagrams over a Stream using inter-frame gaps
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/stream.h>
#include <interfaces/datagram.h>
#include <interfaces/uart.h>

/*
 * Wire format
 * ===========
 * Datagrams are sent over the underlying Stream back to back, each one delimited from the next by a
 * short idle period on the medium (the inter-frame gap). No framing bytes, length prefixes or
 * escaping are added - the raw datagram payload is written verbatim.
 *
 * On transmission the service writes the whole datagram in a single stream write. If the Uart interface
 * of the stream is available, the service waits before each datagram until the medium has been idle for a
 * few frames plus a random backoff. That ends the previous datagram and, with the Uart sensing the medium,
 * avoids colliding with other nodes on a shared medium. Otherwise the medium is kept idle for a
 * configurable gap after each datagram. The idle period is what separates consecutive datagrams on the wire.
 *
 * On reception the service reads from the stream using a timeout. While bytes keep arriving they are
 * accumulated into the current frame. When the stream signals the end of a transfer (STREAM_RET_EOT, e.g.
 * a gap of a few characters detected by an UART receiver timeout) or the stream read times out (the
 * medium went idle for the receive timeout), the current frame is considered complete and returned as a
 * single datagram. The end of transfer delimits frames even with a gap much shorter than the timeout.
 *
 * This pairs naturally with half-duplex UART links (RS485, CAN PHY) where the line idles between
 * bursts of bytes.
 */

typedef enum {
	PROTO_DGSTREAM_RET_OK = 0,
	PROTO_DGSTREAM_RET_FAILED,
} proto_dgstream_ret_t;

typedef struct proto_dgstream {
	Stream *stream;
	/* Uart interface of the stream used to send the idle periods, NULL if not available. */
	Uart *uart;
	Datagram dgram;

	/* Serializes outbound frame writes (and the inter-frame gap) to the underlying stream. */
	SemaphoreHandle_t tx_lock;
	/* Serializes inbound frame reads from the underlying stream. */
	SemaphoreHandle_t rx_lock;

	/* Idle period kept on the medium after a frame is transmitted without the Uart interface, in milliseconds. */
	uint32_t tx_gap_ms;
	/* Stream read timeout used to detect the end of a received frame, in milliseconds. */
	uint32_t rx_timeout_ms;

	/* State of the pseudo-random generator of the transmit backoff. */
	uint32_t backoff_state;
} ProtoDgstream;


/* The @p uart is the Uart interface of the @p stream, it may be NULL. */
proto_dgstream_ret_t proto_dgstream_init(ProtoDgstream *self, Stream *stream, Uart *uart);
proto_dgstream_ret_t proto_dgstream_free(ProtoDgstream *self);

/* Obtain the Datagram interface used to send and receive datagrams over the stream. */
proto_dgstream_ret_t proto_dgstream_get_datagram(ProtoDgstream *self, Datagram **d);

/* Override the inter-frame gap (transmit) and end-of-frame timeout (receive) in milliseconds. */
proto_dgstream_ret_t proto_dgstream_set_tx_gap(ProtoDgstream *self, uint32_t gap_ms);
proto_dgstream_ret_t proto_dgstream_set_rx_timeout(ProtoDgstream *self, uint32_t timeout_ms);
