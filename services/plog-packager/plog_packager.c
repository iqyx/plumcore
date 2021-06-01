/* SPDX-License-Identifier: BSD-2-Clause
 *
 * FIFO in a flash device
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "sha3.h"
#include "chacha20.h"
#include "poly1305.h"

#include <interfaces/mq.h>
#include <types/ndarray.h>
#include "heatshrink_encoder.h"
#include "heatshrink_decoder.h"

#include "plog_packager.h"
#include "pkg.pb.h"
#include <pb_encode.h>

#define MODULE_NAME "plog-packager"

#define HERE u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("at " __FILE__ ":%u") , __LINE__);

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
	HSE_poll_res pres = 0;
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
		// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("encoder_poll size = %u, pres = %u, 0x%02x"), poll_size, pres, self->data[self->data_used]);
		if (pres < 0) {
			return PLOG_PACKAGER_RET_FAILED;
		}
		self->data_used += poll_size;
	} while (pres == HSER_POLL_MORE);

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
		// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("encoder_sink = %u"), sink_size);
		len -= sink_size;
		buf += sink_size;

		if (package_poll_compress_data(self) != PLOG_PACKAGER_RET_OK) {
			return PLOG_PACKAGER_RET_FAILED;
		}
		self->data_used_raw += len;
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
		self->data_used_raw += len;
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
		self->data_used_raw += len;
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
	self->data_used_raw = 0;
	self->header_used = 0;

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


static plog_packager_ret_t package_finish(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	if (self->finished) {
		return PLOG_PACKAGER_RET_FAILED;
	}
	uint8_t buf[64];

	/* Finish LZSS compression and write the rest of the pending data. Now the data
	 * part of the package is completed. */
	package_compress_finish(self);

	/* We may encode the header. Complete HeatshrinkData msg. */
	pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
	pb_encode_tag(&stream, PB_WT_VARINT, HeatshrinkData_lookahead_size_tag);
	pb_encode_varint(&stream, self->parent->hs_lookahead_size);
	pb_encode_tag(&stream, PB_WT_VARINT, HeatshrinkData_window_size_tag);
	pb_encode_varint(&stream, self->parent->hs_window_size);
	pb_encode_tag(&stream, PB_WT_STRING, HeatshrinkData_msg_tag);
	pb_encode_varint(&stream, self->data_used);
	package_prepend_raw_data(self, buf, stream.bytes_written);

	stream = pb_ostream_from_buffer(buf, sizeof(buf));
	pb_encode_tag(&stream, PB_WT_VARINT, PackageData_pkg_index_tag);
	pb_encode_varint(&stream, self->index);
	pb_encode_tag(&stream, PB_WT_VARINT, PackageData_msg_count_tag);
	pb_encode_varint(&stream, self->message_count);
	pb_encode_tag(&stream, PB_WT_STRING, PackageData_heatshrink_tag);
	pb_encode_varint(&stream, self->data_used + self->header_used);
	package_prepend_raw_data(self, buf, stream.bytes_written);

	/** @todo compute MAC here */

	stream = pb_ostream_from_buffer(buf, sizeof(buf));
	pb_encode_tag(&stream, PB_WT_STRING, Package_data_tag);
	pb_encode_varint(&stream, self->data_used + self->header_used);
	package_prepend_raw_data(self, buf, stream.bytes_written);

	/** @todo prepend MAC here */

	package_prepend_raw_data(self, PLOG_PACKAGER_PACKAGE_MAGIC, PLOG_PACKAGER_PACKAGE_MAGIC_SIZE);

	self->finished = true;
	return PLOG_PACKAGER_RET_OK;
}


static plog_packager_ret_t package_publish(struct plog_packager_package *self) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	const char *topic = self->parent->dst_topic;
	if (strlen(topic) == 0) {
		topic = "package";
	}

	struct timespec ts = {0};
	NdArray array;
	ndarray_init_view(&array, DTYPE_BYTE, self->data_used + self->header_used, self->data - self->header_used, self->data_used + self->header_used);
	if (self->parent->mqc->vmt->publish(self->parent->mqc, self->parent->dst_topic, &array, &ts) != MQ_RET_OK) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	return PLOG_PACKAGER_RET_OK;
}


static bool nanopb_write_callback_compressed(pb_ostream_t *stream, const uint8_t *buf, size_t count) {
	struct plog_packager_package *self = (struct plog_packager_package *)stream->state;
	return package_append_compress_data(self, buf, count) == PLOG_PACKAGER_RET_OK;
}


static void encode_message(pb_ostream_t *stream, NdArray *msg, const char *topic) {
	/* Write the message type. Proto file Msg Type enum is the same as ndarray.dtype */
	pb_encode_tag(stream, PB_WT_VARINT, Msg_type_tag);
	pb_encode_varint(stream, msg->dtype);
	/** @todo add time here */

	/* Encode the topic */
	pb_encode_tag(stream, PB_WT_STRING, Msg_topic_tag);
	pb_encode_varint(stream, strlen(topic));
	pb_write(stream, (const uint8_t *)topic, strlen(topic));

	/* Encode the message data */
	pb_encode_tag(stream, PB_WT_STRING, Msg_buf_tag);
	pb_encode_varint(stream, msg->asize * msg->dsize);
	pb_write(stream, msg->buf, msg->asize * msg->dsize);
	// pb_write(stream, test_vector, 32);

}

static plog_packager_ret_t package_add_message(struct plog_packager_package *self, NdArray *msg, const char *topic) {
	if (self == NULL) {
		return PLOG_PACKAGER_RET_NULL;
	}

	pb_ostream_t len_stream = {0};
	encode_message(&len_stream, msg, topic);
	size_t msg_len = len_stream.bytes_written;

	size_t remaining = self->data_size - self->data_used;
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("remaining %u"), remaining);
	if ((msg_len + self->data_used) > (self->data_size / 2)) {
		package_finish(self);
		package_publish(self);
		package_prepare(self);
	}
	
	pb_ostream_t stream = {nanopb_write_callback_compressed, self, self->data_size - self->data_used, 0};

	/* Message header */
	pb_encode_tag(&stream, PB_WT_STRING, RawData_msg_tag);
	pb_encode_varint(&stream, msg_len);
	encode_message(&stream, msg, topic);

	self->message_count++;

	return PLOG_PACKAGER_RET_OK;
}


static void plog_packager_task(void *p) {
	PlogPackager *self = (PlogPackager *)p;

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		struct timespec ts = {0};
		char topic[PLOG_PACKAGER_TOPIC_FILTER_SIZE] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, PLOG_PACKAGER_TOPIC_FILTER_SIZE, &self->rxbuf, &ts) == MQ_RET_OK) {
			package_add_message(&self->package, &self->rxbuf, topic);
		}
	}
	vTaskDelete(NULL);
	self->running = false;
}


plog_packager_ret_t plog_packager_init(PlogPackager *self, Mq *mq) {
	if (u_assert(self != NULL)) {
		return PLOG_PACKAGER_RET_NULL;
	}

	memset(self, 0, sizeof(PlogPackager));
	self->mq = mq;

	return PLOG_PACKAGER_RET_OK;
}


plog_packager_ret_t plog_packager_free(PlogPackager *self) {
	package_free(&self->package);
	heatshrink_encoder_free(self->hs_encoder);
	self->mq = NULL;

	return PLOG_PACKAGER_RET_OK;
}


plog_packager_ret_t plog_packager_start(PlogPackager *self, size_t msg_size, size_t package_size) {
	if (u_assert(self->mq != NULL)) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	/* We are counting packages from the start of the process. Init and prepare the first one. */
	self->package_counter = 0;
	if (package_init(&self->package, self, package_size, PLOG_PACKAGER_HEADER_SIZE) != PLOG_PACKAGER_RET_OK) {
		goto err;
	}
	package_prepare(&self->package);

	/* Create a new MQ client instance we will use to publish messages */
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}
	self->mqc->vmt->subscribe(self->mqc, self->topic_filter);

	/* Using heatshrink to compress data */
	self->hs_window_size = 8;
	self->hs_lookahead_size = 5;
	self->hs_encoder = heatshrink_encoder_alloc(self->hs_window_size, self->hs_lookahead_size);
	
	if (self->hs_encoder == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate compression encoder"));
		return PLOG_PACKAGER_RET_FAILED;
	}

	if (ndarray_init_empty(&self->rxbuf, DTYPE_BYTE, msg_size) != NDARRAY_RET_OK) {
		goto err;
	}

	/* Run the packager main thread. Messages are being received inside. */
	xTaskCreate(plog_packager_task, "plog-packager", configMINIMAL_STACK_SIZE + 512, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("packaging '%s' -> '%s', max pkg size %u B"), self->topic_filter, self->dst_topic, package_size);
	return PLOG_PACKAGER_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start"));
	return PLOG_PACKAGER_RET_FAILED;
}


plog_packager_ret_t plog_packager_stop(PlogPackager *self) {
	if (u_assert(self->running == true)) {
		return PLOG_PACKAGER_RET_FAILED;
	}

	self->can_run = false;
	while (self->running) {
		vTaskDelay(100);
	}

	if (self->mqc) {
		self->mqc->vmt->close(self->mqc);
	}

	ndarray_free(&self->rxbuf);

	return PLOG_PACKAGER_RET_OK;
}


plog_packager_ret_t plog_packager_add_filter(PlogPackager *self, const char *topic_filter, const char *dst_topic) {
	strlcpy(self->topic_filter, topic_filter, PLOG_PACKAGER_TOPIC_FILTER_SIZE);
	strlcpy(self->dst_topic, dst_topic, PLOG_PACKAGER_TOPIC_FILTER_SIZE);
	return PLOG_PACKAGER_RET_OK;
}


plog_packager_ret_t plog_packager_set_nonce(PlogPackager *self, const uint8_t *nonce, size_t len) {
	if (len > PLOG_PACKAGER_NONCE_SIZE) {
		len = PLOG_PACKAGER_NONCE_SIZE;
	}
	memcpy(self->nonce, nonce, len);
	self->nonce_size = len;
	return PLOG_PACKAGER_RET_OK;
}


plog_packager_ret_t plog_packager_set_key(PlogPackager *self, const uint8_t *key, size_t len) {
	if (len > PLOG_PACKAGER_MAX_KEY_SIZE) {
		len = PLOG_PACKAGER_MAX_KEY_SIZE;
	}
	memcpy(self->key, key, len);
	self->key_size = len;
	return PLOG_PACKAGER_RET_OK;
}

