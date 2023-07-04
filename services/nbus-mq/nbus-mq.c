/* SPDX-License-Identifier: BSD-2-Clause
 *
 * NBUS message queue bridge service
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>

#include "blake2.h"
#include "nbus-mq.h"

#define MODULE_NAME "nbus-mq"


static nbus_mq_ret_t prepare_buffer(NbusMq *self, struct nbus_mq_msg_buffer *buf) {
	buf->state = NBUS_MQ_MB_STATE_ACTIVE;
	buf->len = 0;

	/* Start a CBOR array */
	buf->data[buf->len++] = 0x9f;
	return NBUS_MQ_RET_OK;
}


static nbus_mq_ret_t close_buffer(NbusMq *self, struct nbus_mq_msg_buffer *buf) {
	/* Close the last container (array). */
	buf->data[buf->len++] = 0xff;

	buf->state = NBUS_MQ_MB_STATE_FULL;
	return NBUS_MQ_RET_OK;
}


static struct nbus_mq_msg_buffer *get_active_buffer(NbusMq *self) {
	struct nbus_mq_msg_buffer *b = NULL;

	/** @todo lock here */
	for (size_t i = 0; i < NBUS_MQ_MSG_BUFFERS; i++) {
		/* Select the active buffer. */
		if (self->msg_buffers[i].state == NBUS_MQ_MB_STATE_ACTIVE) {
			b = &(self->msg_buffers[i]);
			break;
		}
	}
	if (b == NULL) {
		/* Whoa, no active buffer. Search for an empty buffer and prepare it. */
		for (size_t i = 0; i < NBUS_MQ_MSG_BUFFERS; i++) {
			if (self->msg_buffers[i].state == NBUS_MQ_MB_STATE_EMPTY) {
				b = &(self->msg_buffers[i]);
				prepare_buffer(self, b);
				break;
			}
		}
	}
	/* Note we may return NULL if no buffer can be allocated. */
	/** @todo unlock here */
	return b;
}


static struct nbus_mq_msg_buffer *get_ready_buffer(NbusMq *self) {
	struct nbus_mq_msg_buffer *b = NULL;

	for (size_t i = 0; i < NBUS_MQ_MSG_BUFFERS; i++) {
		if (self->msg_buffers[i].state == NBUS_MQ_MB_STATE_FULL) {
			b = &(self->msg_buffers[i]);
			break;
		}
	}

	return b;
}


static nbus_mq_ret_t save_msg_to_buffer(NbusMq *self, const char *topic, NdArray *ndarray, struct timespec *ts) {

	/* Encode CBOR here */
	/** @todo meh! */
	uint8_t cbor[128];
	CborEncoder encoder;
	cbor_encoder_init(&encoder, cbor, sizeof(cbor), 0);

	cbor_encode_text_stringz(&encoder, "ts");
	cbor_encode_uint(&encoder, ts->tv_sec);
	cbor_encode_text_stringz(&encoder, "to");
	cbor_encode_text_stringz(&encoder, topic);
	cbor_encode_text_stringz(&encoder, "v");
	switch (ndarray->dtype) {
		case DTYPE_FLOAT:
			cbor_encode_float(&encoder, *(float *)ndarray->buf);
			break;
		default:
			cbor_encode_undefined(&encoder);
	}
	size_t cbor_len = cbor_encoder_get_buffer_size(&encoder, cbor);

	struct nbus_mq_msg_buffer *b = get_active_buffer(self);
	if (b == NULL) {
		/* Ok nope, no buffer to save the message to */
		return NBUS_MQ_RET_FAILED;
	}
	/* Always keep one byte for finishing the array and 2 bytes for start/finish of the map. */
	if ((1 + cbor_len + 2 + b->len) > NBUS_MQ_MSG_BUFFER_SIZE) {
		/* We had some buffer but unfortunately there was not enough space.
		 * Try to find a new one. */
		close_buffer(self, b);
		b = get_active_buffer(self);
	}
	if (b == NULL) {
		/* Still nothing, no new buffer can be found (all full). */
		return NBUS_MQ_RET_FAILED;
	}

	/* Start a map. */
	b->data[b->len++] = 0xbf;

	memcpy(b->data + b->len, cbor, cbor_len);
	b->len += cbor_len;

	/* Finish the map. */
	b->data[b->len++] = 0xff;

	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("added len %u to buf %p, cur len %u"), cbor_len, b, b->len);


	/** @todo continue here */
	return NBUS_MQ_RET_OK;
}


static void rx_task(void *p) {
	NbusMq *self = p;

	self->rx_can_run = true;
	self->rx_running = true;
	while (self->rx_can_run) {
		struct timespec ts = {0};
		char topic[NBUS_MQ_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, NBUS_MQ_MAX_TOPIC_LEN, &self->rx_buf, &ts) == MQ_RET_OK) {
			save_msg_to_buffer(self, topic, &self->rx_buf, &ts);
		}
	}
	self->rx_running = false;

	vTaskDelete(NULL);
}


static void nbus_task(void *p) {
	NbusMq *self = p;

	while (true) {
		nbus_endpoint_t ep = 0;
		size_t len = 0;
		nbus_ret_t ret = nbus_channel_receive(&self->channel, &ep, &self->nbus_buf, NBUS_MQ_NBUS_BUF_LEN, &len, 1000);
		/* Respond on endpoint 1 and send the most recent and ready buffer. */
		if (ret == NBUS_RET_OK && ep == 1) {
			struct nbus_mq_msg_buffer *b = get_ready_buffer(self);
			if (b != NULL) {
				nbus_channel_send(&self->channel, ep, b->data, b->len);
				b->state = NBUS_MQ_MB_STATE_EMPTY;
			} else {
				/* Send an empty array */
				nbus_channel_send(&self->channel, ep, "\x9f\xff", 2);
			}
		}
	}

	vTaskDelete(NULL);
}


nbus_mq_ret_t nbus_mq_init(NbusMq *self, Mq *mq, NbusChannel *parent, const char *name) {
	memset(self, 0, sizeof(NbusMq));

	self->mq = mq;

	nbus_channel_init(&self->channel, name);
	nbus_channel_set_parent(&self->channel, parent);
	nbus_add_channel(parent->nbus, &self->channel);

	xTaskCreate(nbus_task, "nbus-mq", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->nbus_task));
	if (self->nbus_task == NULL) {
		return NBUS_RET_FAILED;
	}

	return NBUS_MQ_RET_OK;
}


nbus_mq_ret_t nbus_mq_free(NbusMq *self) {
	(void)self;
	/** @todo stop the nbus task */
	return NBUS_MQ_RET_OK;
}


nbus_mq_ret_t nbus_mq_start(NbusMq *self, const char *topic) {
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}
	self->mqc->vmt->subscribe(self->mqc, topic);

	/** @todo how to allocate the ndarray to hold received messages? */
	if (ndarray_init_empty(&self->rx_buf, DTYPE_UINT8, NBUS_MQ_MSG_BUFFER_SIZE) != NDARRAY_RET_OK) {
		goto err;
	}

	xTaskCreate(rx_task, "nbus-mq-rx", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->rx_task));
	if (self->rx_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create receiving task"));
		goto err;
	}

	return NBUS_MQ_RET_OK;
err:
	/* Not fully started, stop everything. */
	nbus_mq_stop(self);
	return NBUS_MQ_RET_FAILED;
}


nbus_mq_ret_t nbus_mq_stop(NbusMq *self) {

	self->rx_can_run = false;
	while (self->rx_running) {
		vTaskDelay(100);
	}

	if (self->mqc) {
		self->mqc->vmt->close(self->mqc);
	}

	ndarray_free(&self->rx_buf);

	return NBUS_MQ_RET_OK;
}


