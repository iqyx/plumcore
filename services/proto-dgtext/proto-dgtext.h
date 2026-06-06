/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Transport of arbitrary datagrams over a Stream as plain text lines
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
 * Each datagram is sent as a single text line terminated by a newline:
 *
 *     <sync><base64 payload><delim><checksum hex>\n
 *
 *   sync      a single synchronization character ('$'). The receiver discards
 *             everything until it sees this token before it starts collecting a
 *             new datagram.
 *   payload   the raw datagram bytes encoded as base64 (no embedded whitespace).
 *   delim     a single delimiter character ('*') separating the payload from the
 *             trailing checksum.
 *   checksum  the first PROTO_DGTEXT_CKSUM_BYTES bytes of a BLAKE2s digest of the
 *             raw datagram payload, encoded as lowercase hexadecimal. BLAKE2s is a
 *             cryptographically strong yet lightweight keyless hash (single small
 *             context, no lookup tables), a good fit for a resource constrained MCU.
 *
 * Every character on the line is a printable ASCII character, so the line survives
 * transports that are only able to carry ordinary text.
 */

#define PROTO_DGTEXT_SYNC_CHAR '$'
#define PROTO_DGTEXT_CKSUM_DELIM '*'
#define PROTO_DGTEXT_EOL '\n'

/* Number of BLAKE2s digest bytes used as the line checksum and its hex length. */
#define PROTO_DGTEXT_CKSUM_BYTES 4
#define PROTO_DGTEXT_CKSUM_HEX_LEN (PROTO_DGTEXT_CKSUM_BYTES * 2)

/* Base64 encoded length (without the terminating nul) of n raw bytes. */
#define PROTO_DGTEXT_B64_LEN(n) (4 * (((n) + 2) / 3))

/* Number of raw payload bytes encoded and streamed out at once. Must be a multiple of 3 so that
 * base64 padding only ever appears in the final (possibly shorter) chunk. */
#define PROTO_DGTEXT_TX_CHUNK 24

/* Size of the inbound read buffer. Bytes are pulled from the stream in chunks of up to this size
 * to amortize the cost of the stream read call, then consumed one at a time by the line parser. */
#define PROTO_DGTEXT_RX_CHUNK 32

typedef enum {
	PROTO_DGTEXT_RET_OK = 0,
	PROTO_DGTEXT_RET_FAILED,
} proto_dgtext_ret_t;

typedef struct proto_dgtext {
	Stream *stream;
	Datagram dgram;

	/* Serializes outbound line writes to the underlying stream. */
	SemaphoreHandle_t tx_lock;
	/* Serializes inbound datagram reads (parser state and read buffer below). */
	SemaphoreHandle_t rx_lock;

	/* Inbound read buffer. Bytes left here after a datagram is parsed belong to the following
	 * datagram and are consumed by the next read() call, so this state persists between reads. */
	uint8_t rx_chunk[PROTO_DGTEXT_RX_CHUNK];
	size_t rx_chunk_pos;
	size_t rx_chunk_len;
} ProtoDgtext;


proto_dgtext_ret_t proto_dgtext_init(ProtoDgtext *self, Stream *stream);
proto_dgtext_ret_t proto_dgtext_free(ProtoDgtext *self);

/* Obtain the Datagram interface used to send and receive datagrams over the stream. */
proto_dgtext_ret_t proto_dgtext_get_datagram(ProtoDgtext *self, Datagram **d);
