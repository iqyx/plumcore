/*
 * plog message packager/serializer
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include "sha3.h"
#include "chacha20.h"
#include "poly1305.h"

#include "interfaces/plog/descriptor.h"
#include "interfaces/plog/client.h"
#include "heatshrink_encoder.h"
#include "heatshrink_decoder.h"

#include "plog_packager.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "plog-packager"


static plog_packager_ret_t package_init(struct plog_packager_package *self, PlogPackager *parent, size_t data_size, size_t header_size) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}
	if (parent == NULL) {
		return PLOG_PACKAGER_RET_BAD_ARG;
	}

	memset(self, 0, sizeof(struct plog_packager_package));
	self->parent = parent;
	self->data_size = data_size;
	self->header_size = header_size;
	self->data = malloc(data_size + header_size);
	if (self->data == NULL) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* Move the *data pointer forward (package is serialized from the middle
	 * and the header is added afterwards. */
	self->data += self->header_size;

	return PLOG_PACKAGER_RET_OK;
}


static plog_packager_ret_t package_free(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	/* Free the buffer including the header. */
	free(self->data - self->header_size);

	return PLOG_PACKAGER_RET_OK;
}


/* Check if the LZSS compressor has anything to output. If yes, read the compressed
 * data and APPEND it to the buffer moving data_used forward. */
static plog_packager_ret_t package_poll_compress_data(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	size_t poll_size = 0;
	HSD_poll_res pres = 0;
	do {
		if (self->data_used >= self->data_size) {
			return PLOG_PACKAGER_RET_FAILED;
		}
		pres = heatshrink_encoder_poll(
			self->parent->hs_encoder,
			self->data + self->data_used,
			self->data_size - self->data_used,
			&poll_size
		);
		if (pres < 0) {
			return PLOG_PACKAGER_RET_FAILED;
		}
		self->data_used += poll_size;
	} while (pres == HSDR_POLL_MORE);

	return PLOG_PACKAGER_RET_OK;
}


static plog_packager_ret_t package_append_compress_data(struct plog_packager_package *self, const uint8_t *buf, size_t len) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	while (len) {
		/* "discards 'const' qualifier" warning as heatshrink encoder has a non-const buffer input. */
		size_t sink_size;
		HSE_sink_res sres = heatshrink_encoder_sink(self->parent->hs_encoder, buf, len, &sink_size);
		if (sres < 0) {
			return PLOG_PACKAGER_RET_FAILED;
		}
		len -= sink_size;
		buf += sink_size;

		if (package_poll_compress_data(self) != PLOG_PACKAGER_RET_OK) {
			return PLOG_PACKAGER_RET_FAILED;
		}
	}

	return PLOG_PACKAGER_RET_OK;
}


static plog_packager_ret_t package_prepend_raw_data(struct plog_packager_package *self, const uint8_t *buf, size_t len) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	if (len <= (self->header_size - self->header_used)) {
		memcpy(self->data - self->header_used - len, buf, len);
		self->header_used += len;
		return PLOG_PACKAGER_RET_OK;
	}

	return PLOG_PACKAGER_RET_FAILED;
}


static plog_packager_ret_t package_append_raw_data(struct plog_packager_package *self, const uint8_t *buf, size_t len) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	if (len <= (self->data_size - self->data_used)) {
		memcpy(self->data + self->data_used, buf, len);
		self->data_used += len;
		return PLOG_PACKAGER_RET_OK;
	}

	return PLOG_PACKAGER_RET_FAILED;
}


static plog_packager_ret_t package_prepare(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	self->finished = false;

	heatshrink_encoder_reset(self->parent->hs_encoder);

	/* Assign the package id from the current package counter and increment it. */
	self->index = self->parent->package_counter;
	self->parent->package_counter++;

	self->message_count = 0;
	self->data_used = 0;
	self->header_used = 0;

	return PLOG_PACKAGER_RET_OK;
}


static plog_packager_ret_t package_compute_key(struct plog_packager_package *self, uint8_t key[32]) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	if (key == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	/* Compute package key as a SHA3-256 hashed concatenation
	 * of a packager nonce, packager key and a package index. */
	sha3_ctx sha3;
	rhash_sha3_256_init(&sha3);
	rhash_sha3_update(&sha3, self->parent->nonce, PLOG_PACKAGER_NONCE_SIZE);
	rhash_sha3_update(&sha3, self->parent->key, self->parent->key_size);
	rhash_sha3_update(&sha3, self->parent->nonce, PLOG_PACKAGER_NONCE_SIZE);
	uint8_t tmp[4] = {
		0,
		0,
		self->index / 256,
		self->index % 256
	};
	rhash_sha3_update(&sha3, tmp, sizeof(tmp));
	rhash_sha3_final(&sha3, key);

	return PLOG_PACKAGER_RET_OK;
}


static plog_packager_ret_t package_add_mac_tag(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	chacha20_context chacha;

	/* Ok, compute the package key first. We will use it to compute the MAC. */
	uint8_t key[32] = {0};
	if (package_compute_key(self, key) != PLOG_PACKAGER_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* Now use the chacha20 primitive with a NULL nonce and a zero counter
	 * to derive a key for poly1305. */
	chacha20_keysetup(&chacha, key, 256);
	uint8_t nonce[8] = {0};
	chacha20_nonce(&chacha, nonce);
	chacha20_counter(&chacha, 0);

	uint8_t polykey[64] = {0};
	chacha20_keystream(&chacha, polykey);

	uint8_t tag[16] = {0};
	/* Only the first part of the key is used. */
	poly1305(tag, self->data, self->data_used, polykey);

	package_append_raw_data(self, PLOG_PACKAGER_MAC_POLY1305_HEADER, 4);
	package_append_raw_data(self, tag, 16);

	return PLOG_PACKAGER_RET_OK;
}



static plog_packager_ret_t package_compress_finish(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	HSE_finish_res fres = 0;
	while (true) {
		fres = heatshrink_encoder_finish(self->parent->hs_encoder);
		if (fres == HSER_FINISH_MORE) {
			if (package_poll_compress_data(self) != PLOG_PACKAGER_RET_OK) {
				return PLOG_PACKAGER_RET_FAILED;
			}
		} else {
			break;
		}
	}

	return PLOG_PACKAGER_RET_OK;
}


static plog_packager_ret_t package_add_package_header(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	/* Prepend the package index. */
	uint8_t tmp[4] = {
		0,
		0,
		self->index / 256,
		self->index % 256
	};
	if (package_prepend_raw_data(self, tmp, sizeof(tmp)) != PLOG_PACKAGER_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* Nonce of the packager process. */
	if (package_prepend_raw_data(self, self->parent->nonce, PLOG_PACKAGER_NONCE_SIZE) != PLOG_PACKAGER_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* And finally the package header. It is the first one in the serialized stream. */
	if (package_prepend_raw_data(self, PLOG_PACKAGER_PACKAGE_HEADER, 4) != PLOG_PACKAGER_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	return PLOG_PACKAGER_RET_OK;
}


static plog_packager_ret_t package_add_package_trailer(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	return package_append_raw_data(self, PLOG_PACKAGER_PACKAGE_TRAILER, 4);
}


static plog_packager_ret_t package_add_message_list_header(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	uint8_t tmp[4] = {
		self->message_count / 256,
		self->message_count % 256,
		self->data_used / 256,
		self->data_used % 256
	};
	if (package_prepend_raw_data(self, tmp, sizeof(tmp)) != PLOG_PACKAGER_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	if (package_prepend_raw_data(self, PLOG_PACKAGER_MLIST_HEADER1, 4) != PLOG_PACKAGER_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	return PLOG_PACKAGER_RET_FAILED;
}


static plog_packager_ret_t package_finish(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	if (self->finished) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* Finish LZSS compression and write the rest of the pending data. Append a message
	 * list header when the compressed size is known. */
	package_compress_finish(self);
	package_add_message_list_header(self);
	/** @todo encrypt here and add different header if requested */

	/* Prepend package headers. */
	package_add_package_header(self);

	/* And append package trailers. */
	package_add_mac_tag(self);
	package_add_package_trailer(self);

	self->finished = true;

	return PLOG_PACKAGER_RET_OK;
}


static plog_packager_ret_t package_publish(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	if (plog_publish_data(&self->parent->plog, self->parent->dest_topic, self->data - self->header_used, self->data_used + self->header_used) != PLOG_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	return PLOG_PACKAGER_RET_OK;
}



static plog_packager_ret_t package_add_message(struct plog_packager_package *self, const IPlogMessage *msg) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	if (self->data_used >= PLOG_PACKAGER_DATA_SIZE_THR) {
		package_finish(self);

	} else {
		package_append_compress_data(self, (const uint8_t *)"time", 4);
		uint8_t topic_len_bytes[2] = {
			strlen(msg->topic) / 256,
			strlen(msg->topic) % 256
		};
		package_append_compress_data(self, topic_len_bytes, sizeof(topic_len_bytes));

		switch (msg->type) {
			case IPLOG_MESSAGE_TYPE_FLOAT: {
				uint8_t tmp[4];
				memcpy(tmp, &msg->content.cfloat, 4);
				package_append_compress_data(self, PLOG_PACKAGER_M_FLOAT_HEADER, 2);
				package_append_compress_data(self, tmp, sizeof(float));
				break;
			}

			default:
				package_append_compress_data(self, PLOG_PACKAGER_M_NONE_HEADER, 2);
				break;
		}
		self->message_count++;
	}

	return PLOG_PACKAGER_RET_OK;
}


static plog_ret_t plog_packager_recv_event_handler(void *context, const IPlogMessage *msg) {
	PlogPackager *self = (PlogPackager *)context;
	package_add_message(&self->package, msg);

	return PLOG_RET_OK;
}


static void plog_packager_task(void *p) {
	PlogPackager *self = (PlogPackager *)p;

	self->running = true;
	while (self->can_run) {
		plog_receive(&self->plog, 1000);
	}
	vTaskDelete(NULL);
	self->running = false;
}


static void plog_packager_sender_task(void *p) {
	PlogPackager *self = (PlogPackager *)p;

	self->sender_running = true;
	while (self->sender_can_run) {
		if (self->package.finished) {
			package_publish(&self->package);
			package_prepare(&self->package);
		}
		vTaskDelay(100);
	}
	vTaskDelete(NULL);
	self->sender_running = false;
}


plog_packager_ret_t plog_packager_init(PlogPackager *self, const char *topic_filter, const char *dest_topic) {
	if (u_assert(self != NULL)) {
		return PLOG_PACKAGER_RET_NULL;
	}

	memset(self, 0, sizeof(PlogPackager));

	/* Initialize the plog client and set reception callbacks. */
	strlcpy(self->topic_filter, topic_filter, PLOG_PACKAGER_TOPIC_FILTER_SIZE);
	strlcpy(self->dest_topic, dest_topic, PLOG_PACKAGER_TOPIC_FILTER_SIZE);
	Interface *interface;
	if (iservicelocator_query_name_type(locator, "plog-router", ISERVICELOCATOR_TYPE_PLOG_ROUTER, &interface) != ISERVICELOCATOR_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}
	IPlog *iplog = (IPlog *)interface;
	if (plog_open(&self->plog, iplog) != PLOG_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}
	if (plog_set_recv_handler(&self->plog, plog_packager_recv_event_handler, (void *)self) != PLOG_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* We counting packages from the start. Init and prepare the first one. */
	self->package_counter = 0;
	package_init(&self->package, self, PLOG_PACKAGER_DATA_SIZE, PLOG_PACKAGER_HEADER_SIZE);
	package_prepare(&self->package);

	self->hs_encoder = heatshrink_encoder_alloc(6, 5);
	if (self->hs_encoder == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate compression encoder"));
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* Generate package nonce. It is done every time the packager is started. */
	/** @todo */
	memcpy(&self->nonce, "asdfasdfasdfasdf", PLOG_PACKAGER_NONCE_SIZE);

	/* Until a key is set, use the default one. */
	memcpy(self->key, "sample-key", 10);
	self->key_size = 10;

	/* Run the packager main thread. Messages are being received inside. */
	self->can_run = true;
	xTaskCreate(plog_packager_task, "plog-packager", configMINIMAL_STACK_SIZE + 512, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create main task"));
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* Run the packager sending thread. */
	self->sender_can_run = true;
	xTaskCreate(plog_packager_sender_task, "plog-packager-s", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->sender_task));
	if (self->sender_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create sender task"));
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* We can subscribe to the selected messages now. */
	if (plog_subscribe(&self->plog, self->topic_filter) != PLOG_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("service started, packing '%s' to '%s'"), self->topic_filter, self->dest_topic);
	self->initialized = true;
	return PLOG_PACKAGER_RET_OK;
}


plog_packager_ret_t plog_packager_free(PlogPackager *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	if (self->initialized == false) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	self->can_run = false;
	while (self->running) {
		vTaskDelay(10);
	}

	self->sender_can_run = false;
	while (self->sender_running) {
		vTaskDelay(10);
	}

	package_free(&self->package);
	heatshrink_encoder_free(self->hs_encoder);

	self->initialized = false;
	return PLOG_PACKAGER_RET_OK;
}





