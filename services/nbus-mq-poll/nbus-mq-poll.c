/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NBUS2 message queue poll bridge service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Bridges the message queue to an nbus2 endpoint using a poll model. Values received on the
 * configured topic are serialised into CBOR and accumulated into fixed-size batches. A remote host
 * periodically sends a poll request datagram; the service answers each request with the oldest
 * ready batch (or an empty CBOR map when nothing is pending).
 *
 * Each batch is a CBOR map { "h": <device name>, "d": [ {"ts","to","v"}, ... ] } carrying a header
 * with the device name and an array of the serialised messages.
 *
 * The reception side and the request-servicing side run as two independent tasks communicating
 * through a small ring of batch buffers.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <cbor.h>
#include <interfaces/mq.h>
#include <interfaces/datagram.h>
#include <types/ndarray.h>

#include "nbus-mq-poll.h"

#define MODULE_NAME "nbus-mq-poll"


/**********************************************************************************************************************
 * Batch buffer management
 **********************************************************************************************************************/

static nbus_mq_poll_ret_t prepare_buffer(NbusMqPoll *self, struct nbus_mq_poll_msg_buffer *buf) {
	buf->state = NBUS_MQ_POLL_MB_STATE_ACTIVE;
	buf->len = 0;

	/* Encode the batch header: the device name under "h", then open the "d" values array. */
	uint8_t cbor[128];
	CborEncoder encoder;
	cbor_encoder_init(&encoder, cbor, sizeof(cbor), 0);
	cbor_encode_text_stringz(&encoder, "h");
	cbor_encode_text_stringz(&encoder, self->conf.device_name);
	cbor_encode_text_stringz(&encoder, "d");
	size_t cbor_len = cbor_encoder_get_buffer_size(&encoder, cbor);

	/* Start the top level CBOR map. */
	buf->data[buf->len++] = 0xbf;

	memcpy(buf->data + buf->len, cbor, cbor_len);
	buf->len += cbor_len;

	/* Start the values CBOR array. */
	buf->data[buf->len++] = 0x9f;
	return NBUS_MQ_POLL_RET_OK;
}


static nbus_mq_poll_ret_t close_buffer(NbusMqPoll *self, struct nbus_mq_poll_msg_buffer *buf) {
	(void)self;

	/* Close the values array container. */
	buf->data[buf->len++] = 0xff;

	/* Close the top level map container. */
	buf->data[buf->len++] = 0xff;

	buf->state = NBUS_MQ_POLL_MB_STATE_FULL;
	return NBUS_MQ_POLL_RET_OK;
}


static struct nbus_mq_poll_msg_buffer *get_active_buffer(NbusMqPoll *self) {
	/* Reuse the currently active buffer if there is one. */
	for (size_t i = 0; i < NBUS_MQ_POLL_MSG_BUFFERS; i++) {
		if (self->msg_buffers[i].state == NBUS_MQ_POLL_MB_STATE_ACTIVE) {
			return &self->msg_buffers[i];
		}
	}

	/* Otherwise grab an empty buffer and prepare it. */
	for (size_t i = 0; i < NBUS_MQ_POLL_MSG_BUFFERS; i++) {
		if (self->msg_buffers[i].state == NBUS_MQ_POLL_MB_STATE_EMPTY) {
			prepare_buffer(self, &self->msg_buffers[i]);
			return &self->msg_buffers[i];
		}
	}

	/* All buffers are full and waiting to be polled. */
	return NULL;
}


static struct nbus_mq_poll_msg_buffer *get_ready_buffer(NbusMqPoll *self) {
	for (size_t i = 0; i < NBUS_MQ_POLL_MSG_BUFFERS; i++) {
		if (self->msg_buffers[i].state == NBUS_MQ_POLL_MB_STATE_FULL) {
			return &self->msg_buffers[i];
		}
	}
	return NULL;
}


static nbus_mq_poll_ret_t save_msg_to_buffer(NbusMqPoll *self, const char *topic, const NdArray *ndarray, const struct timespec *ts) {
	/* Serialise the message (timestamp, topic, value) into a standalone CBOR map. */
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

	struct nbus_mq_poll_msg_buffer *b = get_active_buffer(self);
	if (b == NULL) {
		return NBUS_MQ_POLL_RET_FAILED;
	}

	/* Keep 2 bytes to close the array and map, plus 2 bytes for the value map delimiters. When the
	 * current batch cannot hold the value, close it and start a fresh one. */
	if ((2 + cbor_len + 2 + b->len) > NBUS_MQ_POLL_MSG_BUFFER_SIZE) {
		close_buffer(self, b);
		b = get_active_buffer(self);
	}
	if (b == NULL) {
		return NBUS_MQ_POLL_RET_FAILED;
	}

	/* Wrap the serialised value in its own map inside the values array. */
	b->data[b->len++] = 0xbf;
	memcpy(b->data + b->len, cbor, cbor_len);
	b->len += cbor_len;
	b->data[b->len++] = 0xff;

	return NBUS_MQ_POLL_RET_OK;
}


/**********************************************************************************************************************
 * Message queue reception task
 **********************************************************************************************************************/

static void rx_task(void *p) {
	NbusMqPoll *self = p;

	self->rx_can_run = true;
	self->rx_running = true;
	while (self->rx_can_run) {
		struct timespec ts = {0};
		char topic[NBUS_MQ_POLL_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, NBUS_MQ_POLL_MAX_TOPIC_LEN, &self->rx_buf, &ts) == MQ_RET_OK) {
			save_msg_to_buffer(self, topic, &self->rx_buf, &ts);
		}
	}
	self->rx_running = false;

	vTaskDelete(NULL);
}


/**********************************************************************************************************************
 * NBUS poll request servicing task
 **********************************************************************************************************************/

static void nbus_task(void *p) {
	NbusMqPoll *self = p;

	self->nbus_can_run = true;
	self->nbus_running = true;
	while (self->nbus_can_run) {
		size_t len = NBUS_MQ_POLL_NBUS_BUF_LEN;
		struct datagram_msg rxmsg = {0};
		if (self->conf.d->vmt->read(self->conf.d, self->nbus_buf, &len, &rxmsg) != DATAGRAM_RET_OK) {
			continue;
		}

		/* A poll request arrived. Answer it to the requester's address. */
		struct datagram_msg txmsg = {0};
		txmsg.addr_size = 4;
		txmsg.dst_port = rxmsg.src_port;
		memcpy(txmsg.dst_addr, rxmsg.src_addr, 4);

		/* Send the oldest ready batch and release it, or an empty map when nothing is pending. */
		struct nbus_mq_poll_msg_buffer *b = get_ready_buffer(self);
		if (b != NULL) {
			self->conf.d->vmt->write(self->conf.d, b->data, b->len, &txmsg);
			b->state = NBUS_MQ_POLL_MB_STATE_EMPTY;
		} else {
			self->conf.d->vmt->write(self->conf.d, "\xbf\xff", 2, &txmsg);
		}
	}
	self->nbus_running = false;

	vTaskDelete(NULL);
}


/**********************************************************************************************************************
 * Service lifecycle
 **********************************************************************************************************************/

nbus_mq_poll_ret_t nbus_mq_poll_init(NbusMqPoll *self, const struct nbus_mq_poll_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return NBUS_MQ_POLL_RET_NULL;
	}
	memset(self, 0, sizeof(NbusMqPoll));
	memcpy(&self->conf, conf, sizeof(struct nbus_mq_poll_conf));

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return NBUS_MQ_POLL_RET_OK;
}


nbus_mq_poll_ret_t nbus_mq_poll_free(NbusMqPoll *self) {
	if (u_assert(self != NULL)) {
		return NBUS_MQ_POLL_RET_FAILED;
	}

	return NBUS_MQ_POLL_RET_OK;
}


nbus_mq_poll_ret_t nbus_mq_poll_start(NbusMqPoll *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->conf.mq != NULL) ||
	    u_assert(self->conf.d != NULL) ||
	    u_assert(self->conf.topic != NULL) ||
	    u_assert(self->conf.device_name != NULL)) {
		return NBUS_MQ_POLL_RET_FAILED;
	}

	strlcpy(self->topic, self->conf.topic, NBUS_MQ_POLL_MAX_TOPIC_LEN);

	/* Open a message queue client to receive the subscribed values. */
	self->mqc = self->conf.mq->vmt->open(self->conf.mq);
	if (self->mqc == NULL) {
		goto err;
	}
	self->mqc->vmt->subscribe(self->mqc, self->topic);

	/* Receive scratch buffer for a single incoming value. */
	if (ndarray_init_empty(&self->rx_buf, DTYPE_UINT8, NBUS_MQ_POLL_MSG_BUFFER_SIZE) != NDARRAY_RET_OK) {
		goto err;
	}

	xTaskCreate(nbus_task, "nbus-mq-poll", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->nbus_task));
	if (self->nbus_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create nbus task"));
		goto err;
	}

	xTaskCreate(rx_task, "nbus-mq-poll-rx", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->rx_task));
	if (self->rx_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create receiving task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("started, subscribed to '%s'"), self->topic);
	return NBUS_MQ_POLL_RET_OK;
err:
	/* Not fully started, stop everything. */
	nbus_mq_poll_stop(self);
	return NBUS_MQ_POLL_RET_FAILED;
}


nbus_mq_poll_ret_t nbus_mq_poll_stop(NbusMqPoll *self) {
	if (u_assert(self != NULL)) {
		return NBUS_MQ_POLL_RET_FAILED;
	}

	/* Stop the reception task. */
	self->rx_can_run = false;
	while (self->rx_running) {
		vTaskDelay(100);
	}

	/* Stop the NBUS servicing task. */
	self->nbus_can_run = false;
	while (self->nbus_running) {
		vTaskDelay(100);
	}

	if (self->mqc != NULL) {
		self->mqc->vmt->close(self->mqc);
	}

	ndarray_free(&self->rx_buf);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return NBUS_MQ_POLL_RET_OK;
}
