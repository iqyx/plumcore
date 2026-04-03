/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reliable transport of stream data over datagrams
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include <cbor.h>

#include <interfaces/flash.h>
#include <interfaces/datagram.h>

#include "proto-stream.h"

#define MODULE_NAME "proto-stream"


/**********************************************************************************************************************
 * Stream interface API
 **********************************************************************************************************************/

static stream_ret_t stream_write(Stream *self, const void *buf, size_t size) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL)) {
		return STREAM_RET_FAILED;
	}
	ProtoStream *proto_stream = self->parent;

	if (size == 0) {
		return STREAM_RET_OK;
	}

	xSemaphoreTake(proto_stream->txlock, portMAX_DELAY);
	xStreamBufferSend(proto_stream->txbuf, buf, size, 0);
	xSemaphoreGive(proto_stream->txlock);

	return STREAM_RET_OK;
}


static stream_ret_t stream_write_timeout(Stream *self, const void *buf, size_t size, size_t *written, uint32_t timeout_ms) {
	(void)timeout_ms;

	ProtoStream *proto_stream = self->parent;
	if (size == 0) {
		return STREAM_RET_OK;
	}

	/** @todo timeout_ms is not really used correctly. It could wait double the time, worst case. */
	if (xSemaphoreTake(proto_stream->txlock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
		size_t txw = xStreamBufferSend(proto_stream->txbuf, buf, size, pdMS_TO_TICKS(timeout_ms));
		if (written != NULL) {
			*written = txw;
		}
		xSemaphoreGive(proto_stream->txlock);
		return STREAM_RET_OK;
	}
	if (written != NULL) {
		*written = 0;
	}

	return STREAM_RET_FAILED;
}


static stream_ret_t stream_read_timeout(Stream *self, void *buf, size_t size, size_t *read, uint32_t timeout_ms) {
	ProtoStream *proto_stream = self->parent;

	if (timeout_ms != portMAX_DELAY) {
		timeout_ms = pdMS_TO_TICKS(timeout_ms);
	}

	for (size_t i = 0; i < size; i++) {
		uint8_t c;
		/* Waiting with timeout for the first byte only. */
		size_t r = xStreamBufferReceive(proto_stream->rxbuf, &c, sizeof(c), i ? 0 : timeout_ms);

		if (r == 0) {
			if (read != NULL) {
				*read = i;
			}
			return i ? STREAM_RET_OK : STREAM_RET_TIMEOUT;
		} else {
			((uint8_t *)buf)[i] = c;
		}
	}
	if (read != NULL) {
		*read = size;
	}

	return STREAM_RET_OK;
}


static stream_ret_t stream_read(Stream *self, void *buf, size_t size, size_t *read) {
	(void)self;
	(void)buf;
	(void)size;

	return stream_read_timeout(self, buf, size, read, portMAX_DELAY);

	return STREAM_RET_OK;
}


static const struct stream_vmt proto_stream_stream_vmt = {
	.write = stream_write,
	.read = stream_read,
	.write_timeout = stream_write_timeout,
	.read_timeout = stream_read_timeout
};


/**********************************************************************************************************************
 * Protocol implementation
 **********************************************************************************************************************/

static proto_stream_ret_t proto_stream_process(ProtoStream *self, uint8_t *buf, size_t len) {
	CborParser parser;
	CborValue map;

	cbor_parser_init(buf, len, 0, &parser, &map);
	if (cbor_value_is_map(&map)) {

		/* Prepare a top level map for the response. basically every received datagram
		 * is considered valid if it contains a map, including an empty map. */
		CborEncoder encoder;
		cbor_encoder_init(&encoder, self->tx_buf, PROTO_STREAM_CBOR_LEN, 0);
		CborEncoder encoder_map;
		cbor_encoder_create_map(&encoder, &encoder_map, CborIndefiniteLength);

		/* Check if the initiator requests data to be transmitted. */
		CborValue req;
		cbor_value_map_find_value(&map, "get", &req);
		if (cbor_value_is_valid(&req) && cbor_value_is_integer(&req)) {
			int req_block = 0;
			cbor_value_get_int(&req, &req_block);
			if ((uint32_t)req_block == self->tx_last_block) {
				/* Skip everything, DO NOT read from the buffer, retransmit the last block. */
				goto retransmit;
			}
			self->tx_last_block = req_block;
			size_t req_len = PROTO_STREAM_BLOCK_LEN;

			uint8_t b[PROTO_STREAM_BLOCK_LEN];
			req_len = xStreamBufferReceive(self->txbuf, b, req_len, 0);

			cbor_encode_text_stringz(&encoder_map, "d");
			cbor_encode_byte_string(&encoder_map, b, req_len);
		}

		/* Check if the initiator is trying to put (upload) some data. */
		CborValue put;
		cbor_value_map_find_value(&map, "put", &put);
		if (cbor_value_is_valid(&put) && cbor_value_is_integer(&put)) {
			int put_block = 0;
			cbor_value_get_int(&put, &put_block);

			/* Always ack no matter what. */
			cbor_encode_text_stringz(&encoder_map, "a");
			cbor_encode_uint(&encoder_map, put_block);

			if ((uint32_t)put_block != self->rx_last_block) {
				/* New block is received. Save the data and increase the count. */
				CborValue cb;
				cbor_value_map_find_value(&map, "d", &cb);

				uint8_t b[PROTO_STREAM_BLOCK_LEN];
				size_t put_len = sizeof(b);
				cbor_value_copy_byte_string(&cb, b, &put_len, NULL);

				if (put_len > 0) {
					xStreamBufferSend(self->rxbuf, b, put_len, 0);
				}
				self->rx_last_block = put_block;
			}
		}

		/* Include buffer statistics in every response. */
		cbor_encode_text_stringz(&encoder_map, "rdf");
		cbor_encode_uint(&encoder_map, xStreamBufferSpacesAvailable(self->rxbuf));
		cbor_encode_text_stringz(&encoder_map, "tda");
		cbor_encode_uint(&encoder_map, xStreamBufferBytesAvailable(self->txbuf));

		/* Close the container and send the map in all circumstances (even if empty). */
		cbor_encoder_close_container(&encoder, &encoder_map);
		self->tx_len = cbor_encoder_get_buffer_size(&encoder, self->tx_buf);

retransmit:
		struct datagram_msg txmsg = {0};
		txmsg.addr_size = 4;
		txmsg.dst_port = self->src_port;
		memcpy(&txmsg.dst_addr, &self->src_addr, 4);
		self->d->vmt->write(self->d, self->tx_buf, self->tx_len, &txmsg);

		return PROTO_STREAM_RET_OK;
	}

	/* Top level value is not a map. */
	return PROTO_STREAM_RET_FAILED;
}


static void proto_stream_task(void *p) {
	ProtoStream *self = p;

	while (true) {
		size_t len = PROTO_STREAM_CBOR_LEN;
		struct datagram_msg rxmsg = {0};
		if (self->d->vmt->read(self->d, &self->rx_buf, &len, &rxmsg) == DATAGRAM_RET_OK) {
			self->src_port = rxmsg.src_port;
			memcpy(&self->src_addr, &rxmsg.src_addr, 4);
			proto_stream_process(self, self->rx_buf, len);
		}
	}

	vTaskDelete(NULL);
}


proto_stream_ret_t proto_stream_init(ProtoStream *self, Datagram *d, size_t rxbuf_size, size_t txbuf_size) {
	memset(self, 0, sizeof(ProtoStream));

	self->d = d;

	/* Allocate IPC primitives. */
	self->rxbuf = xStreamBufferCreate(rxbuf_size, 1);
	self->txbuf = xStreamBufferCreate(txbuf_size, 1);
	if (self->rxbuf == NULL || self->txbuf == NULL) {
		return PROTO_STREAM_RET_FAILED;
	}
	self->txlock = xSemaphoreCreateMutex();
	if (self->txlock == NULL) {
		return PROTO_STREAM_RET_FAILED;
	}

	self->stream.parent = self;
	self->stream.vmt = &proto_stream_stream_vmt;

	xTaskCreate(proto_stream_task, "proto-stream", configMINIMAL_STACK_SIZE + 512, (void *)self, 1, &(self->proto_task));
	if (self->proto_task == NULL) {
		return PROTO_STREAM_RET_FAILED;
	}

	return PROTO_STREAM_RET_OK;
}


proto_stream_ret_t proto_stream_free(ProtoStream *self) {
	(void)self;
	return PROTO_STREAM_RET_OK;
}



