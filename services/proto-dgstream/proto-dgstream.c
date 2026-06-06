/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Framing of datagrams over a Stream using inter-frame gaps
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/stream.h>
#include <interfaces/datagram.h>

#include "proto-dgstream.h"

#define MODULE_NAME "proto-dgstream"


/**********************************************************************************************************************
 * Helpers
 **********************************************************************************************************************/

/* Consume and discard the remainder of the current frame until the medium goes idle. Used to
 * resynchronize after a frame did not fit into the caller's buffer. */
static void drain_frame(ProtoDgstream *self) {
	uint8_t tmp[32];
	size_t read = 0;
	while (self->stream->vmt->read_timeout(self->stream, tmp, sizeof(tmp), &read,
	                                       self->rx_timeout_ms) == STREAM_RET_OK) {
		;
	}
}


/**********************************************************************************************************************
 * Datagram interface API
 **********************************************************************************************************************/

static datagram_ret_t dgram_write(Datagram *self, const void *buf, size_t len, const struct datagram_msg *msg) {
	(void)msg;
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL)) {
		return DATAGRAM_RET_BAD_ARG;
	}
	ProtoDgstream *proto = self->parent;

	if (len == 0) {
		return DATAGRAM_RET_BAD_ARG;
	}

	xSemaphoreTake(proto->tx_lock, portMAX_DELAY);

	datagram_ret_t ret = DATAGRAM_RET_OK;

	/* Emit the whole datagram as a single contiguous burst on the medium. */
	if (proto->stream->vmt->write(proto->stream, buf, len) != STREAM_RET_OK) {
		ret = DATAGRAM_RET_FAILED;
		goto out;
	}

	/* Keep the medium idle for the inter-frame gap so the receiver sees a delimiting timeout
	 * before the next datagram starts. */
	if (proto->tx_gap_ms > 0) {
		vTaskDelay(pdMS_TO_TICKS(proto->tx_gap_ms));
	}

out:
	xSemaphoreGive(proto->tx_lock);
	return ret;
}


static datagram_ret_t dgram_read(Datagram *self, void *buf, size_t *len, struct datagram_msg *msg) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len != NULL)) {
		return DATAGRAM_RET_BAD_ARG;
	}
	ProtoDgstream *proto = self->parent;
	uint8_t *out = buf;
	size_t cap = *len;

	if (msg != NULL) {
		memset(msg, 0, sizeof(struct datagram_msg));
	}

	xSemaphoreTake(proto->rx_lock, portMAX_DELAY);

	size_t off = 0;
	datagram_ret_t ret = DATAGRAM_RET_OK;

	/* Accumulate bytes into the frame until the medium goes idle. A read timeout with at least one
	 * byte already buffered delimits the end of the current frame; a read timeout with nothing
	 * buffered simply means no frame has started yet, keep waiting. */
	while (true) {
		size_t read = 0;
		stream_ret_t sret = proto->stream->vmt->read_timeout(proto->stream, out + off, cap - off,
		                                                     &read, proto->rx_timeout_ms);
		if (sret == STREAM_RET_OK) {
			off += read;
			if (off >= cap) {
				/* The frame does not fit into the caller's buffer. Discard the rest and report
				 * a failure rather than splitting it across reads. */
				drain_frame(proto);
				ret = DATAGRAM_RET_FAILED;
				break;
			}
		} else if (sret == STREAM_RET_TIMEOUT) {
			if (off > 0) {
				break;
			}
		} else {
			ret = DATAGRAM_RET_FAILED;
			break;
		}
	}

	xSemaphoreGive(proto->rx_lock);

	if (ret == DATAGRAM_RET_OK) {
		*len = off;
	}
	return ret;
}


static const struct datagram_vmt proto_dgstream_datagram_vmt = {
	.write = dgram_write,
	.read = dgram_read,
};


/**********************************************************************************************************************
 * Public API
 **********************************************************************************************************************/

proto_dgstream_ret_t proto_dgstream_init(ProtoDgstream *self, Stream *stream) {
	if (u_assert(self != NULL) ||
	    u_assert(stream != NULL)) {
		return PROTO_DGSTREAM_RET_FAILED;
	}
	memset(self, 0, sizeof(ProtoDgstream));

	self->stream = stream;
	self->tx_gap_ms = CONFIG_SERVICE_PROTO_DGSTREAM_TX_GAP_MS;
	self->rx_timeout_ms = CONFIG_SERVICE_PROTO_DGSTREAM_RX_TIMEOUT_MS;

	self->dgram.parent = self;
	self->dgram.vmt = &proto_dgstream_datagram_vmt;

	self->tx_lock = xSemaphoreCreateMutex();
	self->rx_lock = xSemaphoreCreateMutex();
	if (self->tx_lock == NULL || self->rx_lock == NULL) {
		proto_dgstream_free(self);
		return PROTO_DGSTREAM_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("service started, tx gap = %u ms, rx timeout = %u ms"),
		(unsigned int)self->tx_gap_ms, (unsigned int)self->rx_timeout_ms);
	return PROTO_DGSTREAM_RET_OK;
}


proto_dgstream_ret_t proto_dgstream_free(ProtoDgstream *self) {
	if (u_assert(self != NULL)) {
		return PROTO_DGSTREAM_RET_FAILED;
	}

	if (self->tx_lock != NULL) {
		vSemaphoreDelete(self->tx_lock);
		self->tx_lock = NULL;
	}
	if (self->rx_lock != NULL) {
		vSemaphoreDelete(self->rx_lock);
		self->rx_lock = NULL;
	}

	return PROTO_DGSTREAM_RET_OK;
}


proto_dgstream_ret_t proto_dgstream_get_datagram(ProtoDgstream *self, Datagram **d) {
	if (u_assert(self != NULL) ||
	    u_assert(d != NULL)) {
		return PROTO_DGSTREAM_RET_FAILED;
	}
	*d = &self->dgram;

	return PROTO_DGSTREAM_RET_OK;
}


proto_dgstream_ret_t proto_dgstream_set_tx_gap(ProtoDgstream *self, uint32_t gap_ms) {
	if (u_assert(self != NULL)) {
		return PROTO_DGSTREAM_RET_FAILED;
	}
	self->tx_gap_ms = gap_ms;

	return PROTO_DGSTREAM_RET_OK;
}


proto_dgstream_ret_t proto_dgstream_set_rx_timeout(ProtoDgstream *self, uint32_t timeout_ms) {
	if (u_assert(self != NULL)) {
		return PROTO_DGSTREAM_RET_FAILED;
	}
	self->rx_timeout_ms = timeout_ms;

	return PROTO_DGSTREAM_RET_OK;
}
