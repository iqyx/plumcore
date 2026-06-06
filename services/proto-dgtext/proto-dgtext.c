/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Transport of arbitrary datagrams over a Stream as plain text lines
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include <base64.h>
#include <blake2s.h>

#include <interfaces/stream.h>
#include <interfaces/datagram.h>

#include "proto-dgtext.h"

#define MODULE_NAME "proto-dgtext"


/**********************************************************************************************************************
 * Helpers
 **********************************************************************************************************************/

static void byte2hex(const uint8_t *in, size_t in_size, char *out) {
	static const char a[] = "0123456789abcdef";

	for (size_t i = 0; i < in_size; i++) {
		out[i * 2] = a[in[i] >> 4];
		out[i * 2 + 1] = a[in[i] & 0xf];
	}
}


/* Return the next inbound byte, refilling the read buffer from the stream in chunks when drained.
 * Blocks until at least one byte is available. */
static stream_ret_t next_byte(ProtoDgtext *self, char *c) {
	if (self->rx_chunk_pos >= self->rx_chunk_len) {
		size_t read = 0;
		if (self->stream->vmt->read_timeout(self->stream, self->rx_chunk, sizeof(self->rx_chunk), &read,
		                                     portMAX_DELAY) != STREAM_RET_OK || read == 0) {
			return STREAM_RET_FAILED;
		}
		self->rx_chunk_len = read;
		self->rx_chunk_pos = 0;
	}
	*c = (char)self->rx_chunk[self->rx_chunk_pos++];

	return STREAM_RET_OK;
}


/**********************************************************************************************************************
 * Datagram interface API (sending direction)
 **********************************************************************************************************************/

static datagram_ret_t dgram_write(Datagram *self, const void *buf, size_t len, const struct datagram_msg *msg) {
	(void)msg;
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL)) {
		return DATAGRAM_RET_BAD_ARG;
	}
	ProtoDgtext *proto = self->parent;
	const uint8_t *data = buf;

	if (len > CONFIG_SERVICE_PROTO_DGTEXT_MAX_DATAGRAM_LEN) {
		return DATAGRAM_RET_BAD_ARG;
	}

	xSemaphoreTake(proto->tx_lock, portMAX_DELAY);

	/* The line is streamed out directly, no full-line buffer is held. The checksum is computed
	 * incrementally over the raw payload while it is being encoded chunk by chunk. */
	blake2s_state cksum;
	blake2s_init(&cksum, PROTO_DGTEXT_CKSUM_BYTES);

	datagram_ret_t ret = DATAGRAM_RET_OK;

	/* Start of frame. */
	char sync = PROTO_DGTEXT_SYNC_CHAR;
	if (proto->stream->vmt->write(proto->stream, &sync, 1) != STREAM_RET_OK) {
		ret = DATAGRAM_RET_FAILED;
		goto out;
	}

	/* Encode the payload in fixed size chunks, updating the checksum on the way. As the chunk size
	 * is a multiple of 3, base64 padding can only appear in the final, possibly shorter, chunk. */
	for (size_t off = 0; off < len; off += PROTO_DGTEXT_TX_CHUNK) {
		size_t chunk = len - off;
		if (chunk > PROTO_DGTEXT_TX_CHUNK) {
			chunk = PROTO_DGTEXT_TX_CHUNK;
		}

		blake2s_update(&cksum, &data[off], chunk);

		char enc[PROTO_DGTEXT_B64_LEN(PROTO_DGTEXT_TX_CHUNK) + 1];
		if (base64encode(&data[off], chunk, enc, sizeof(enc)) != BASE64_RET_OK) {
			ret = DATAGRAM_RET_FAILED;
			goto out;
		}
		if (proto->stream->vmt->write(proto->stream, enc, strlen(enc)) != STREAM_RET_OK) {
			ret = DATAGRAM_RET_FAILED;
			goto out;
		}
	}

	/* Trailer: delimiter, checksum and end of line, emitted in a single write. */
	uint8_t digest[PROTO_DGTEXT_CKSUM_BYTES];
	blake2s_final(&cksum, digest);

	char trailer[1 + PROTO_DGTEXT_CKSUM_HEX_LEN + 1];
	trailer[0] = PROTO_DGTEXT_CKSUM_DELIM;
	byte2hex(digest, sizeof(digest), &trailer[1]);
	trailer[1 + PROTO_DGTEXT_CKSUM_HEX_LEN] = PROTO_DGTEXT_EOL;
	if (proto->stream->vmt->write(proto->stream, trailer, sizeof(trailer)) != STREAM_RET_OK) {
		ret = DATAGRAM_RET_FAILED;
		goto out;
	}

out:
	xSemaphoreGive(proto->tx_lock);
	return ret;
}


/* Receive and decode a single datagram synchronously into out (capacity out_cap), returning the
 * decoded length in out_len. The caller must hold the rx lock; see dgram_read. */
static datagram_ret_t dgram_read_frame(ProtoDgtext *proto, uint8_t *out, size_t out_cap, size_t *out_len) {
	blake2s_state cksum;
	size_t out_off;
	char group[4];
	size_t gcount;
	char c;

	/* Discard everything until the synchronization token marks the start of a frame. */
	do {
		if (next_byte(proto, &c) != STREAM_RET_OK) {
			return DATAGRAM_RET_FAILED;
		}
	} while (c != PROTO_DGTEXT_SYNC_CHAR);

restart:
	blake2s_init(&cksum, PROTO_DGTEXT_CKSUM_BYTES);
	out_off = 0;
	gcount = 0;

	/* Decode the base64 payload group by group straight into the caller's buffer, updating the
	 * checksum over the decoded bytes as we go. As the encoder emits padding only in the final
	 * group, every full four-character group decodes to exactly three bytes. */
	while (true) {
		if (next_byte(proto, &c) != STREAM_RET_OK) {
			return DATAGRAM_RET_FAILED;
		}
		if (c == PROTO_DGTEXT_SYNC_CHAR) {
			/* A new frame start, abandon the current one and resynchronize. */
			goto restart;
		}
		if (c == PROTO_DGTEXT_CKSUM_DELIM) {
			break;
		}
		if (c == PROTO_DGTEXT_EOL) {
			/* End of line before the checksum delimiter, malformed frame. */
			return DATAGRAM_RET_FAILED;
		}

		group[gcount++] = c;
		if (gcount == sizeof(group)) {
			size_t n = out_cap - out_off;
			if (base64decode(group, gcount, &out[out_off], &n) != BASE64_RET_OK) {
				return DATAGRAM_RET_FAILED;
			}
			blake2s_update(&cksum, &out[out_off], n);
			out_off += n;
			gcount = 0;
		}
	}

	/* Decode the trailing partial group, if any. It carries the base64 padding. */
	if (gcount > 0) {
		size_t n = out_cap - out_off;
		if (base64decode(group, gcount, &out[out_off], &n) != BASE64_RET_OK) {
			return DATAGRAM_RET_FAILED;
		}
		blake2s_update(&cksum, &out[out_off], n);
		out_off += n;
	}

	/* Collect the trailing checksum until end of line and verify it against the computed one. */
	uint8_t digest[PROTO_DGTEXT_CKSUM_BYTES];
	blake2s_final(&cksum, digest);
	char expected[PROTO_DGTEXT_CKSUM_HEX_LEN];
	byte2hex(digest, sizeof(digest), expected);

	char got[PROTO_DGTEXT_CKSUM_HEX_LEN];
	size_t got_len = 0;
	while (true) {
		if (next_byte(proto, &c) != STREAM_RET_OK) {
			return DATAGRAM_RET_FAILED;
		}
		if (c == PROTO_DGTEXT_EOL) {
			break;
		}
		if (c == PROTO_DGTEXT_SYNC_CHAR) {
			goto restart;
		}
		if (got_len >= sizeof(got)) {
			/* Checksum longer than expected, malformed frame. */
			return DATAGRAM_RET_FAILED;
		}
		got[got_len++] = c;
	}

	if (got_len != PROTO_DGTEXT_CKSUM_HEX_LEN ||
	    memcmp(expected, got, PROTO_DGTEXT_CKSUM_HEX_LEN) != 0) {
		return DATAGRAM_RET_FAILED;
	}

	*out_len = out_off;
	return DATAGRAM_RET_OK;
}


static datagram_ret_t dgram_read(Datagram *self, void *buf, size_t *len, struct datagram_msg *msg) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len != NULL)) {
		return DATAGRAM_RET_BAD_ARG;
	}
	ProtoDgtext *proto = self->parent;

	if (msg != NULL) {
		memset(msg, 0, sizeof(struct datagram_msg));
	}

	/* This call borrows the caller's thread to receive a single datagram synchronously. As the
	 * underlying stream is already buffered, there is no need for a dedicated receive task. The rx
	 * lock serializes concurrent readers, which share the persistent read buffer. */
	xSemaphoreTake(proto->rx_lock, portMAX_DELAY);
	size_t out_len = 0;
	datagram_ret_t ret = dgram_read_frame(proto, buf, *len, &out_len);
	xSemaphoreGive(proto->rx_lock);

	if (ret == DATAGRAM_RET_OK) {
		*len = out_len;
	}

	return ret;
}


static const struct datagram_vmt proto_dgtext_datagram_vmt = {
	.write = dgram_write,
	.read = dgram_read,
};


/**********************************************************************************************************************
 * Public API
 **********************************************************************************************************************/

proto_dgtext_ret_t proto_dgtext_init(ProtoDgtext *self, Stream *stream) {
	if (u_assert(self != NULL) ||
	    u_assert(stream != NULL)) {
		return PROTO_DGTEXT_RET_FAILED;
	}
	memset(self, 0, sizeof(ProtoDgtext));

	self->stream = stream;

	self->dgram.parent = self;
	self->dgram.vmt = &proto_dgtext_datagram_vmt;

	self->tx_lock = xSemaphoreCreateMutex();
	self->rx_lock = xSemaphoreCreateMutex();
	if (self->tx_lock == NULL || self->rx_lock == NULL) {
		proto_dgtext_free(self);
		return PROTO_DGTEXT_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("service started, max datagram = %u B"),
		(unsigned int)CONFIG_SERVICE_PROTO_DGTEXT_MAX_DATAGRAM_LEN);
	return PROTO_DGTEXT_RET_OK;
}


proto_dgtext_ret_t proto_dgtext_free(ProtoDgtext *self) {
	if (u_assert(self != NULL)) {
		return PROTO_DGTEXT_RET_FAILED;
	}

	if (self->tx_lock != NULL) {
		vSemaphoreDelete(self->tx_lock);
		self->tx_lock = NULL;
	}
	if (self->rx_lock != NULL) {
		vSemaphoreDelete(self->rx_lock);
		self->rx_lock = NULL;
	}

	return PROTO_DGTEXT_RET_OK;
}


proto_dgtext_ret_t proto_dgtext_get_datagram(ProtoDgtext *self, Datagram **d) {
	if (u_assert(self != NULL) ||
	    u_assert(d != NULL)) {
		return PROTO_DGTEXT_RET_FAILED;
	}
	*d = &self->dgram;

	return PROTO_DGTEXT_RET_OK;
}
