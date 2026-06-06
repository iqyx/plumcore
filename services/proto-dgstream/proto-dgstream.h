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

/*
 * Wire format
 * ===========
 * Datagrams are sent over the underlying Stream back to back, each one delimited from the next by a
 * short idle period on the medium (the inter-frame gap). No framing bytes, length prefixes or
 * escaping are added - the raw datagram payload is written verbatim.
 *
 * On transmission the service writes the whole datagram in a single stream write and then keeps the
 * medium idle for a configurable gap before the next datagram may be sent. The gap is what separates
 * consecutive datagrams on the wire.
 *
 * On reception the service reads from the stream using a timeout. While bytes keep arriving they are
 * accumulated into the current frame. When the stream read times out (the medium went idle, e.g.
 * signalled by an UART end-of-transfer/receiver-timeout interrupt) the current frame is considered
 * complete and returned as a single datagram.
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
	Datagram dgram;

	/* Serializes outbound frame writes (and the inter-frame gap) to the underlying stream. */
	SemaphoreHandle_t tx_lock;
	/* Serializes inbound frame reads from the underlying stream. */
	SemaphoreHandle_t rx_lock;

	/* Idle period kept on the medium after a frame is transmitted, in milliseconds. */
	uint32_t tx_gap_ms;
	/* Stream read timeout used to detect the end of a received frame, in milliseconds. */
	uint32_t rx_timeout_ms;
} ProtoDgstream;


proto_dgstream_ret_t proto_dgstream_init(ProtoDgstream *self, Stream *stream);
proto_dgstream_ret_t proto_dgstream_free(ProtoDgstream *self);

/* Obtain the Datagram interface used to send and receive datagrams over the stream. */
proto_dgstream_ret_t proto_dgstream_get_datagram(ProtoDgstream *self, Datagram **d);

/* Override the inter-frame gap (transmit) and end-of-frame timeout (receive) in milliseconds. */
proto_dgstream_ret_t proto_dgstream_set_tx_gap(ProtoDgstream *self, uint32_t gap_ms);
proto_dgstream_ret_t proto_dgstream_set_rx_timeout(ProtoDgstream *self, uint32_t timeout_ms);
