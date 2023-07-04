/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus channel CBOR C&C implementation
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <string.h>
#include <stdint.h>

#include <main.h>

#include "cbor.h"
#include "cc-rpc.h"

#define MODULE_NAME "cc-rpc"

cbor_rpc_ret_t cbor_rpc_init(CborRpc *self) {
	memset(self, 0, sizeof(CborRpc));

	return CBOR_RPC_RET_OK;
}


cbor_rpc_ret_t cbor_rpc_add_handler(CborRpc *self, struct cbor_rpc_handler *handler) {
	if (handler == NULL) {
		return CBOR_RPC_RET_FAILED;
	}
	handler->next = self->handlers;
	self->handlers = handler;
	return CBOR_RPC_RET_OK;
}


cbor_rpc_ret_t cbor_rpc_parse(CborRpc *self, void *buf, size_t len) {
	if (len > CBOR_RPC_REQ_SIZE) {
		return CBOR_RPC_RET_FAILED;
	}
	memcpy(self->request, buf, len);

	/* There is a single map in the request. Parse it and check. */
	if (cbor_parser_init(self->request, len, 0, &self->parser, &self->parser_map) != CborNoError ||
	    !cbor_value_is_map(&self->parser_map)) {
		return CBOR_RPC_RET_FAILED;
	}

	return CBOR_RPC_RET_OK;
}


cbor_rpc_ret_t cbor_rpc_call_handler(CborRpc *self, const struct cbor_rpc_handler *handler, CborValue *value) {
	if (cbor_value_is_null(value)) {
		switch (handler->htype) {
			case CBOR_RPC_HTYPE_STRING: {
				/** @todo reading strings supported only up to 64 chars of base64 */
				char buf[64] = {0};
				if (handler->v_string.read != NULL &&
				    handler->v_string.read(self, buf, sizeof(buf)) == CBOR_RPC_RET_OK) {
					cbor_encode_text_stringz(&self->encoder_map, buf);
					return CBOR_RPC_RET_OK;
				}
				break;
			}
			case CBOR_RPC_HTYPE_BOOL: {
				bool v = false;
				if (handler->v_bool.read != NULL &&
				    handler->v_bool.read(self, &v) == CBOR_RPC_RET_OK) {
					cbor_encode_boolean(&self->encoder_map, v);
					return CBOR_RPC_RET_OK;
				}
				break;
			}
			case CBOR_RPC_HTYPE_INT: {
				int32_t v = 0;
				if (handler->v_int.read != NULL &&
				    handler->v_int.read(self, &v) == CBOR_RPC_RET_OK) {
					cbor_encode_int(&self->encoder_map, (int)v);
					return CBOR_RPC_RET_OK;
				}
				break;
			}
			default:
				return CBOR_RPC_RET_FAILED;
		}
	}

	if (cbor_value_is_text_string(value)) {
		/* Write string request. */
		char buf[64] = {0};
		size_t len = sizeof(buf);
		cbor_value_copy_text_string(value, buf, &len, NULL);
		if (handler->htype == CBOR_RPC_HTYPE_STRING &&
		    handler->v_string.write != NULL &&
		    handler->v_string.write(self, buf) == CBOR_RPC_RET_OK) {
			cbor_encode_text_stringz(&self->encoder_map, buf);
			return CBOR_RPC_RET_OK;
		}
	}

	if (cbor_value_is_integer(value)) {
		int32_t i = 0;
		cbor_value_get_int(value, (int *)&i);
		if (handler->htype == CBOR_RPC_HTYPE_INT &&
		    handler->v_int.write != NULL &&
		    handler->v_int.write(self, i) == CBOR_RPC_RET_OK) {
			cbor_encode_int(&self->encoder_map, (int)i);
			return CBOR_RPC_RET_OK;
		}
	}

	if (cbor_value_is_boolean(value)) {
		bool b = 0;
		cbor_value_get_boolean(value, &b);
		if (handler->htype == CBOR_RPC_HTYPE_BOOL &&
		    handler->v_bool.write != NULL &&
		    handler->v_bool.write(self, b) == CBOR_RPC_RET_OK) {
			cbor_encode_boolean(&self->encoder_map, b);
			return CBOR_RPC_RET_OK;
		}
	}

	return CBOR_RPC_RET_FAILED;
}


cbor_rpc_ret_t cbor_rpc_run(CborRpc *self) {
	cbor_encoder_init(&self->encoder, self->response, CBOR_RPC_RESP_SIZE, 0);
	cbor_encoder_create_map(&self->encoder, &self->encoder_map, CborIndefiniteLength);

	/* Append mandatory fields. */
	// cbor_encode_text_stringz(&self->encoder_map, "src");
	// cbor_encode_int(&self->encoder_map, self->address);

	/* Main parsing loop, iterate over the map and check for read requests, write requests and functions. */
	CborValue value;
	if (cbor_value_enter_container(&self->parser_map, &value) != CborNoError) {
		/* Cannot enter the container. */
		return CBOR_RPC_RET_FAILED;
	}

	while (!cbor_value_at_end(&value)) {
		/* Skip all keys which are not strings. We cannot parse them. */
		if (!cbor_value_is_text_string(&value)) {
			return CBOR_RPC_RET_FAILED;
		}

		bool found = false;
		struct cbor_rpc_handler *handler = self->handlers;
		while (handler != NULL) {
			bool eq = false;
			if (cbor_value_text_string_equals(&value, handler->name, &eq) != CborNoError) {
				return CBOR_RPC_RET_FAILED;
			}

			if (eq) {
				found = true;

				/* Advance to the value, we are going to process it. */
				cbor_value_advance(&value);

				cbor_encode_text_stringz(&self->encoder_map, handler->name);
				if (cbor_rpc_call_handler(self, handler, &value) != CBOR_RPC_RET_OK) {
					cbor_encode_null(&self->encoder_map);
				}

				/* Advance to the next key. */
				cbor_value_advance(&value);

				break;
			}
			handler = handler->next;
		}
		if (!found) {
			/** @todo No key matched, unknown handler, respond properly. */

			/* Advance to the next key. */
			cbor_value_advance(&value);
			cbor_value_advance(&value);
		}
		/* And regardless of the outcome, advance to the next key and break the while. */
	}
	return CBOR_RPC_RET_OK;
}


cbor_rpc_ret_t cbor_rpc_get_response(CborRpc *self, void **buf, size_t *len) {
	/* All items from the request were processed. Complete the response and send it. */
	cbor_encoder_close_container(&self->encoder, &self->encoder_map);
	*len = cbor_encoder_get_buffer_size(&self->encoder, self->response);
	*buf = self->response;

	return CBOR_RPC_RET_OK;

}

