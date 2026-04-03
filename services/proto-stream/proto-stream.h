/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Reliable transport of stream data over datagrams
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


#define PROTO_STREAM_CBOR_LEN 256
#define PROTO_STREAM_BLOCK_LEN 220

typedef enum {
	PROTO_STREAM_RET_OK = 0,
	PROTO_STREAM_RET_FAILED,
} proto_stream_ret_t;


typedef struct proto_stream {
	Datagram *d;
	Stream stream;

	TaskHandle_t proto_task;

	StreamBufferHandle_t rxbuf;
	StreamBufferHandle_t txbuf;
	SemaphoreHandle_t txlock;

	uint8_t rx_buf[PROTO_STREAM_CBOR_LEN];
	uint8_t tx_buf[PROTO_STREAM_CBOR_LEN];
	size_t tx_len;
	uint32_t tx_last_block;
	uint32_t rx_last_block;

	uint8_t src_addr[4];
	uint32_t src_port;

} ProtoStream;


proto_stream_ret_t proto_stream_init(ProtoStream *self, Datagram *d, size_t rxbuf_size, size_t txbuf_size);
proto_stream_ret_t proto_stream_free(ProtoStream *self);
