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
#include "blake2s-siv.h"
#include <main.h>


#define NBUS_TIMEOUT_MS 10000
#define NBUS_PBUF_COUNT 4
#define NBUS_PBUF_DATA_SIZE 1050

typedef enum {
	NBUS_RET_OK = 0,
	NBUS_RET_FAILED,
	NBUS_RET_BAD_PARAM,
	NBUS_RET_VOID,
	NBUS_RET_INVALID,
	NBUS_RET_BIG,
	NBUS_RET_TIMEOUT,
} nbus_ret_t;


struct nbus_pbuf {
	bool used;

	uint8_t ke[B2S_KE_LEN];
	uint8_t km[B2S_KM_LEN];

	uint8_t *buf;
	size_t buf_size;
	size_t buf_len;
};


typedef struct nbus {
	Stream *stream;
	const uint8_t *mac_key;
	size_t mac_key_len;

	struct nbus_pbuf pbufs[NBUS_PBUF_COUNT];

	TaskHandle_t mac_task;
	TaskHandle_t hk_task;
} Nbus;


nbus_ret_t nbus_init(Nbus *self, Stream *stream);
nbus_ret_t nbus_set_mac_key(Nbus *self, const uint8_t *mac_key, size_t mac_key_len);


struct nbus_pbuf *nbus_pbuf_allocate(Nbus *self);
nbus_ret_t nbus_pbuf_release(Nbus *self, struct nbus_pbuf *pbuf);
