/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus2 messaging bus implementation
 *
 * Copyright (c) 2023-2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <interfaces/stream.h>
#include <interfaces/datagram.h>
#include "blake2s-siv.h"
#include <main.h>
#include "pbuf.h"


#define NBUS_TIMEOUT_MS 10000
#define NBUS_PBUF_COUNT 4
#define NBUS_PBUF_DATA_SIZE 1050
#define NBUS_SOCKET_COUNT 4
#define NBUS_SOCKET_TX_QUEUE_LEN 2
#define NBUS_SOCKET_RX_QUEUE_LEN 2

typedef enum {
	NBUS_RET_OK = 0,
	NBUS_RET_FAILED,
	NBUS_RET_BAD_PARAM,
	NBUS_RET_VOID,
	NBUS_RET_INVALID,
	NBUS_RET_BIG,
	NBUS_RET_TIMEOUT,
} nbus_ret_t;

typedef struct nbus Nbus;
struct nbus_socket {
	bool used;
	bool enabled;
	Nbus *parent;
	Datagram datagram;

	QueueHandle_t tx_queue;
	QueueHandle_t rx_queue;

	uint8_t *local_id;
	uint8_t *local_id_mask;
	uint32_t local_ep;

	uint8_t *remote_id;
	uint8_t *remote_id_mask;
	uint32_t remote_ep;
};

typedef struct nbus {
	Stream *stream;
	const uint8_t *mac_key;
	size_t mac_key_len;

	SemaphoreHandle_t pbuf_lock;
	struct nbus_pbuf pbufs[NBUS_PBUF_COUNT];

	SemaphoreHandle_t socket_lock;
	struct nbus_socket sockets[NBUS_SOCKET_COUNT];

	TaskHandle_t mac_task;
	TaskHandle_t hk_task;

} Nbus;


nbus_ret_t nbus_init(Nbus *self, Stream *stream);
nbus_ret_t nbus_set_mac_key(Nbus *self, const uint8_t *mac_key, size_t mac_key_len);

struct nbus_pbuf *nbus_pbuf_allocate(Nbus *self);
nbus_ret_t nbus_pbuf_release(Nbus *self, struct nbus_pbuf *pbuf);

struct nbus_socket *nbus_socket_allocate(Nbus *self);
nbus_ret_t nbus_socket_release(Nbus *self, struct nbus_socket *socket);
nbus_ret_t nbus_socket_bind(struct nbus_socket *socket, const uint8_t *id, uint8_t ep);
