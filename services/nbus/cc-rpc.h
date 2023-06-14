/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus channel CBOR C&C implementation
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "cbor.h"

#define CBOR_RPC_REQ_SIZE 256
#define CBOR_RPC_RESP_SIZE 256
#define CBOR_RPC_BROADCAST_ADDR 0

typedef enum {
	CBOR_RPC_RET_OK = 0,
	CBOR_RPC_RET_FAILED,
	/* We did nothing and nothing failed. */
	CBOR_RPC_RET_NULL,
} cbor_rpc_ret_t;

enum cbor_rpc_htype {
	CBOR_RPC_HTYPE_NONE = 0,
	CBOR_RPC_HTYPE_INT,
	CBOR_RPC_HTYPE_STRING,
	CBOR_RPC_HTYPE_BOOL,
	CBOR_RPC_HTYPE_FUNCTION,
};

typedef struct cbor_rpc CborRpc;

struct cbor_rpc_handler_int {
	cbor_rpc_ret_t (*read)(CborRpc *self, int32_t *value);
	cbor_rpc_ret_t (*write)(CborRpc *self, int32_t value);
};

struct cbor_rpc_handler_string {
	cbor_rpc_ret_t (*read)(CborRpc *self, char *buf, size_t buf_size);
	cbor_rpc_ret_t (*write)(CborRpc *self, const char *buf);
};

struct cbor_rpc_handler_bool {
	cbor_rpc_ret_t (*read)(CborRpc *self, bool *value);
	cbor_rpc_ret_t (*write)(CborRpc *self, bool value);
};

struct cbor_rpc_handler_function {
	cbor_rpc_ret_t (*call)(CborRpc *self);
};

struct cbor_rpc_handler {
	const char *name;
	enum cbor_rpc_htype htype;
	union {
		struct cbor_rpc_handler_int v_int;
		struct cbor_rpc_handler_string v_string;
		struct cbor_rpc_handler_bool v_bool;
		struct cbor_rpc_handler_function function;
	};
	struct cbor_rpc_handler *next;
};

typedef struct cbor_rpc {
	/* A pointer used to store the parent owning instance. It is used to allow
	 * instance specific handlers. */
	void *parent;

	struct cbor_rpc_handler *handlers;

	uint8_t request[CBOR_RPC_REQ_SIZE];
	uint8_t response[CBOR_RPC_RESP_SIZE];

	CborParser parser;
	CborValue parser_map;
	CborEncoder encoder;
	CborEncoder encoder_map;
} CborRpc;

typedef enum {
	BASE64_RET_OK = 0,
	BASE64_RET_BUF_SMALL,
	BASE64_RET_INVALID,
	BASE64_RET_BUF_OVERFLOW,
} base64_ret_t;


cbor_rpc_ret_t cbor_rpc_init(CborRpc *self);
cbor_rpc_ret_t cbor_rpc_add_handler(CborRpc *self, struct cbor_rpc_handler *handler);
cbor_rpc_ret_t cbor_rpc_parse(CborRpc *self, void *buf, size_t len);
cbor_rpc_ret_t cbor_rpc_call_handler(CborRpc *self, const struct cbor_rpc_handler *handler, CborValue *value);
cbor_rpc_ret_t cbor_rpc_run(CborRpc *self);
cbor_rpc_ret_t cbor_rpc_get_response(CborRpc *self, void **buf, size_t *len);

