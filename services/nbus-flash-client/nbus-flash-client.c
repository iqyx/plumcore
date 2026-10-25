/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NBUS flash access client service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Client of the nbus-flash access protocol. A partition of a remote nbus-flash service is opened and exposed
 * as a local Flash interface, every flash operation is translated to a request to the remote service. Requests
 * are sent and their responses received synchronously in the context of the calling thread.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>
#include <cbor.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/flash.h>
#include <interfaces/datagram.h>

#include "nbus-flash-client.h"

#define MODULE_NAME "nbus-flash-client"


/**********************************************************************************************************************
 * Request/response exchange
 **********************************************************************************************************************/

/* A request to the remote service. Only the members which are set are encoded. */
struct nbus_flash_client_req {
	const char *c;
	const char *n;
	bool sid;
	bool range;
	size_t addr;
	size_t len;
	const void *d;

	/* Do not log a failure of the request, the caller reports it. */
	bool quiet;
};


static bool cbor_map_get_uint(CborValue *map, const char *key, uint32_t *i) {
	CborValue value;
	uint64_t uint = 0;
	if (cbor_value_map_find_value(map, key, &value) != CborNoError ||
	    !cbor_value_is_unsigned_integer(&value) ||
	    cbor_value_get_uint64(&value, &uint) != CborNoError) {
		return false;
	}
	*i = uint;
	return true;
}


static size_t nbus_flash_client_encode(NbusFlashClient *self, const struct nbus_flash_client_req *req) {
	CborEncoder encoder;
	CborEncoder map;
	cbor_encoder_init(&encoder, self->buf, sizeof(self->buf), 0);
	cbor_encoder_create_map(&encoder, &map, CborIndefiniteLength);
	cbor_encode_text_stringz(&map, "id");
	cbor_encode_uint(&map, self->req_id);
	cbor_encode_text_stringz(&map, "c");
	cbor_encode_text_stringz(&map, req->c);
	if (req->n != NULL) {
		cbor_encode_text_stringz(&map, "n");
		cbor_encode_text_stringz(&map, req->n);
	}
	if (req->sid) {
		cbor_encode_text_stringz(&map, "sid");
		cbor_encode_uint(&map, self->sid);
	}
	if (req->range) {
		cbor_encode_text_stringz(&map, "addr");
		cbor_encode_uint(&map, req->addr);
		cbor_encode_text_stringz(&map, "len");
		cbor_encode_uint(&map, req->len);
	}
	if (req->d != NULL) {
		cbor_encode_text_stringz(&map, "d");
		cbor_encode_byte_string(&map, req->d, req->len);
	}
	if (cbor_encoder_close_container(&encoder, &map) != CborNoError) {
		return 0;
	}
	return cbor_encoder_get_buffer_size(&encoder, self->buf);
}


/* Send the request and receive its response into @p reply, retried if no response is received in time. The
 * response is parsed in place in the buffer, it is valid until the next request. Fails if the response carries an
 * error, times out if no response is received after all retries. */
static nbus_flash_client_ret_t nbus_flash_client_request(NbusFlashClient *self, const struct nbus_flash_client_req *req, CborParser *parser, CborValue *reply) {
	/* Retries of the request share the identifier, any of their responses is accepted. */
	self->req_id++;
	for (uint32_t attempt = 0; attempt < NBUS_FLASH_CLIENT_RETRIES; attempt++) {
		size_t len = nbus_flash_client_encode(self, req);
		if (len == 0) {
			return NBUS_FLASH_CLIENT_RET_FAILED;
		}
		if (self->conf.d->vmt->write(self->conf.d, self->buf, len, NULL) != DATAGRAM_RET_OK) {
			continue;
		}

		/* Skip late responses of earlier requests until a matching one is received or the read times out. A
		 * response with no identifier (a service not echoing it) is accepted. */
		while (true) {
			len = sizeof(self->buf);
			if (self->conf.d->vmt->read(self->conf.d, self->buf, &len, NULL) != DATAGRAM_RET_OK) {
				break;
			}
			if (cbor_parser_init(self->buf, len, 0, parser, reply) != CborNoError ||
			    !cbor_value_is_map(reply)) {
				continue;
			}
			uint32_t id = 0;
			if (cbor_map_get_uint(reply, "id", &id) && id != self->req_id) {
				continue;
			}

			CborValue err;
			if (cbor_value_map_find_value(reply, "err", &err) == CborNoError && cbor_value_is_text_string(&err)) {
				char msg[32] = {0};
				size_t msg_len = sizeof(msg) - 1;
				cbor_value_copy_text_string(&err, msg, &msg_len, NULL);
				if (!req->quiet) {
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%s failed: %s"), req->c, msg);
				}
				return NBUS_FLASH_CLIENT_RET_FAILED;
			}
			return NBUS_FLASH_CLIENT_RET_OK;
		}
	}
	if (!req->quiet) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%s: no response"), req->c);
	}
	return NBUS_FLASH_CLIENT_RET_TIMEOUT;
}


/* Get the block size of level @p i of the opened partition. */
static flash_ret_t nbus_flash_client_info(NbusFlashClient *self, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	CborParser parser;
	CborValue reply;
	CborValue sizes;
	CborValue e;
	if (nbus_flash_client_request(self, &(const struct nbus_flash_client_req) {.c = "info", .n = self->name}, &parser, &reply) != NBUS_FLASH_CLIENT_RET_OK ||
	    cbor_value_map_find_value(&reply, "sizes", &sizes) != CborNoError ||
	    !cbor_value_is_array(&sizes) ||
	    cbor_value_enter_container(&sizes, &e) != CborNoError) {
		return FLASH_RET_FAILED;
	}
	for (uint32_t j = 0; !cbor_value_at_end(&e); j++) {
		if (j == i) {
			uint32_t s = 0;
			uint32_t o = 0;
			if (!cbor_value_is_map(&e) ||
			    !cbor_map_get_uint(&e, "s", &s) ||
			    !cbor_map_get_uint(&e, "ops", &o)) {
				return FLASH_RET_FAILED;
			}
			*size = s;
			*ops = o;
			return FLASH_RET_OK;
		}
		if (cbor_value_advance(&e) != CborNoError) {
			return FLASH_RET_FAILED;
		}
	}
	return FLASH_RET_BAD_ARG;
}


/**********************************************************************************************************************
 * Flash interface
 **********************************************************************************************************************/

static flash_ret_t nbus_flash_client_get_size(Flash *flash, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	NbusFlashClient *self = flash->parent;
	if (self->sid == 0) {
		return FLASH_RET_FAILED;
	}
	return nbus_flash_client_info(self, i, size, ops);
}


static flash_ret_t nbus_flash_client_erase(Flash *flash, const size_t addr, size_t len) {
	NbusFlashClient *self = flash->parent;
	if (self->sid == 0) {
		return FLASH_RET_FAILED;
	}
	self->cache_len = 0;

	CborParser parser;
	CborValue reply;
	if (nbus_flash_client_request(self, &(const struct nbus_flash_client_req) {.c = "erase", .sid = true, .range = true, .addr = addr, .len = len}, &parser, &reply) != NBUS_FLASH_CLIENT_RET_OK) {
		return FLASH_RET_FAILED;
	}
	return FLASH_RET_OK;
}


static flash_ret_t nbus_flash_client_write(Flash *flash, const size_t addr, const void *buf, size_t len) {
	NbusFlashClient *self = flash->parent;
	if (self->sid == 0) {
		return FLASH_RET_FAILED;
	}
	self->cache_len = 0;

	for (size_t pos = 0; pos < len; pos += NBUS_FLASH_CLIENT_BLOCK_LEN) {
		size_t n = (len - pos < NBUS_FLASH_CLIENT_BLOCK_LEN) ? len - pos : NBUS_FLASH_CLIENT_BLOCK_LEN;
		CborParser parser;
		CborValue reply;
		if (nbus_flash_client_request(self, &(const struct nbus_flash_client_req) {.c = "write", .sid = true, .range = true, .addr = addr + pos, .len = n, .d = (const uint8_t *)buf + pos}, &parser, &reply) != NBUS_FLASH_CLIENT_RET_OK) {
			return FLASH_RET_FAILED;
		}
	}
	return FLASH_RET_OK;
}


static flash_ret_t nbus_flash_client_read(Flash *flash, const size_t addr, void *buf, size_t len) {
	NbusFlashClient *self = flash->parent;
	if (self->sid == 0) {
		return FLASH_RET_FAILED;
	}
	if (addr > self->size || len > self->size - addr) {
		return FLASH_RET_BAD_ARG;
	}

	size_t pos = addr;
	uint8_t *dst = buf;
	while (len > 0) {
		/* Read the whole block containing the position if it is not cached. */
		if (pos < self->cache_addr || pos >= self->cache_addr + self->cache_len) {
			size_t block = pos - pos % NBUS_FLASH_CLIENT_BLOCK_LEN;
			size_t n = (self->size - block < NBUS_FLASH_CLIENT_BLOCK_LEN) ? self->size - block : NBUS_FLASH_CLIENT_BLOCK_LEN;
			self->cache_len = 0;

			CborParser parser;
			CborValue reply;
			CborValue d;
			size_t d_len = sizeof(self->cache);
			nbus_flash_client_ret_t ret = nbus_flash_client_request(self, &(const struct nbus_flash_client_req) {.c = "read", .sid = true, .range = true, .addr = block, .len = n, .quiet = true}, &parser, &reply);
			if (ret == NBUS_FLASH_CLIENT_RET_TIMEOUT) {
				return FLASH_RET_TIMEOUT;
			}
			if (ret != NBUS_FLASH_CLIENT_RET_OK ||
			    cbor_value_map_find_value(&reply, "d", &d) != CborNoError ||
			    !cbor_value_is_byte_string(&d) ||
			    cbor_value_copy_byte_string(&d, self->cache, &d_len, NULL) != CborNoError ||
			    d_len != n) {
				return FLASH_RET_FAILED;
			}
			self->cache_addr = block;
			self->cache_len = n;
		}

		size_t n = self->cache_addr + self->cache_len - pos;
		if (n > len) {
			n = len;
		}
		memcpy(dst, self->cache + (pos - self->cache_addr), n);
		dst += n;
		pos += n;
		len -= n;
	}
	return FLASH_RET_OK;
}


static const struct flash_vmt nbus_flash_client_flash_vmt = {
	.get_size = nbus_flash_client_get_size,
	.erase = nbus_flash_client_erase,
	.write = nbus_flash_client_write,
	.read = nbus_flash_client_read,
};


/**********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

nbus_flash_client_ret_t nbus_flash_client_init(NbusFlashClient *self, const struct nbus_flash_client_conf *conf) {
	if (self == NULL || conf == NULL) {
		return NBUS_FLASH_CLIENT_RET_NULL;
	}
	memset(self, 0, sizeof(NbusFlashClient));
	memcpy(&self->conf, conf, sizeof(struct nbus_flash_client_conf));

	self->flash.vmt = &nbus_flash_client_flash_vmt;
	self->flash.parent = self;

	return NBUS_FLASH_CLIENT_RET_OK;
}


nbus_flash_client_ret_t nbus_flash_client_free(NbusFlashClient *self) {
	if (self == NULL) {
		return NBUS_FLASH_CLIENT_RET_NULL;
	}
	return nbus_flash_client_close(self);
}


nbus_flash_client_ret_t nbus_flash_client_list(NbusFlashClient *self, uint32_t i, char *name, size_t size) {
	if (u_assert(self != NULL) ||
	    u_assert(name != NULL)) {
		return NBUS_FLASH_CLIENT_RET_NULL;
	}

	CborParser parser;
	CborValue reply;
	CborValue d;
	CborValue e;
	if (nbus_flash_client_request(self, &(const struct nbus_flash_client_req) {.c = "list"}, &parser, &reply) != NBUS_FLASH_CLIENT_RET_OK ||
	    cbor_value_map_find_value(&reply, "d", &d) != CborNoError ||
	    !cbor_value_is_array(&d) ||
	    cbor_value_enter_container(&d, &e) != CborNoError) {
		return NBUS_FLASH_CLIENT_RET_FAILED;
	}
	for (uint32_t j = 0; !cbor_value_at_end(&e); j++) {
		if (j == i) {
			CborValue n;
			size_t len = size;
			if (!cbor_value_is_map(&e) ||
			    cbor_value_map_find_value(&e, "n", &n) != CborNoError ||
			    !cbor_value_is_text_string(&n) ||
			    cbor_value_copy_text_string(&n, name, &len, NULL) != CborNoError) {
				return NBUS_FLASH_CLIENT_RET_FAILED;
			}
			return NBUS_FLASH_CLIENT_RET_OK;
		}
		if (cbor_value_advance(&e) != CborNoError) {
			return NBUS_FLASH_CLIENT_RET_FAILED;
		}
	}
	return NBUS_FLASH_CLIENT_RET_NOT_FOUND;
}


nbus_flash_client_ret_t nbus_flash_client_open(NbusFlashClient *self, const char *name) {
	if (u_assert(self != NULL) ||
	    u_assert(name != NULL)) {
		return NBUS_FLASH_CLIENT_RET_NULL;
	}

	nbus_flash_client_close(self);
	if (strlen(name) >= sizeof(self->name)) {
		return NBUS_FLASH_CLIENT_RET_FAILED;
	}
	strcpy(self->name, name);

	CborParser parser;
	CborValue reply;
	uint32_t sid = 0;
	if (nbus_flash_client_request(self, &(const struct nbus_flash_client_req) {.c = "open", .n = self->name}, &parser, &reply) != NBUS_FLASH_CLIENT_RET_OK ||
	    !cbor_map_get_uint(&reply, "sid", &sid) ||
	    sid == 0) {
		return NBUS_FLASH_CLIENT_RET_FAILED;
	}
	self->sid = sid;

	/* Level 0 is the whole partition. */
	flash_block_ops_t ops;
	if (nbus_flash_client_info(self, 0, &self->size, &ops) != FLASH_RET_OK) {
		nbus_flash_client_close(self);
		return NBUS_FLASH_CLIENT_RET_FAILED;
	}
	self->cache_len = 0;

	return NBUS_FLASH_CLIENT_RET_OK;
}


nbus_flash_client_ret_t nbus_flash_client_close(NbusFlashClient *self) {
	if (u_assert(self != NULL)) {
		return NBUS_FLASH_CLIENT_RET_NULL;
	}
	if (self->sid == 0) {
		return NBUS_FLASH_CLIENT_RET_OK;
	}

	/* The session is forgotten even if the request fails, the remote service reuses it eventually. */
	CborParser parser;
	CborValue reply;
	nbus_flash_client_ret_t ret = nbus_flash_client_request(self, &(const struct nbus_flash_client_req) {.c = "close", .sid = true}, &parser, &reply);
	self->sid = 0;
	self->size = 0;
	self->cache_len = 0;

	return ret;
}
