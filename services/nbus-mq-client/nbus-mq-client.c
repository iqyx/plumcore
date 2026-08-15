/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NBUS2 message queue poll client service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Client counterpart to the nbus-mq-poll service. It periodically sends a poll request to a remote
 * nbus-mq-poll endpoint over nbus2, receives a batch of serialised values in response, decodes it
 * and republishes every carried value to a local message queue.
 *
 * A batch is a CBOR map { "h": <device name>, "d": [ {"ts","to","v"}, ... ] }. Each array element
 * carries a timestamp ("ts"), the original topic ("to") and a floating point value ("v"). An empty
 * map (nothing pending on the remote side) is decoded to no values.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <cbor.h>
#include <interfaces/mq.h>
#include <interfaces/datagram.h>
#include <types/ndarray.h>

#include "nbus-mq-client.h"

#define MODULE_NAME "nbus-mq-client"


/**********************************************************************************************************************
 * Batch decoding and republishing
 **********************************************************************************************************************/

/* Publish a single decoded value to the message queue, applying the configured topic prefix. */
static void publish_value(NbusMqClient *self, const char *topic, float value, time_t ts_sec) {
	/* Sized to hold "<prefix>/<topic>" plus the separator and terminator without truncation. */
	char full[NBUS_MQ_CLIENT_MAX_PREFIX_LEN + NBUS_MQ_CLIENT_MAX_TOPIC_LEN + 2] = {0};
	if (self->topic_prefix[0] != '\0') {
		snprintf(full, sizeof(full), "%s/%s", self->topic_prefix, topic);
	} else {
		strlcpy(full, topic, sizeof(full));
	}

	struct timespec ts = {0};
	ts.tv_sec = ts_sec;
	NdArray array;
	ndarray_init_view(&array, DTYPE_FLOAT, 1, &value, sizeof(value));
	self->mqc->vmt->publish(self->mqc, full, &array, &ts);
}


/* Decode a received batch and republish every value it carries. */
static void process_batch(NbusMqClient *self, const uint8_t *buf, size_t len) {
	CborParser parser;
	CborValue root;
	if (cbor_parser_init(buf, len, 0, &parser, &root) != CborNoError || !cbor_value_is_map(&root)) {
		return;
	}

	/* Locate the "d" values array. Absent on an empty batch. */
	CborValue array;
	if (cbor_value_map_find_value(&root, "d", &array) != CborNoError || !cbor_value_is_array(&array)) {
		return;
	}

	CborValue it;
	if (cbor_value_enter_container(&array, &it) != CborNoError) {
		return;
	}

	while (!cbor_value_at_end(&it)) {
		if (!cbor_value_is_map(&it)) {
			/* Not the expected per-value map, skip the element. */
			if (cbor_value_advance(&it) != CborNoError) {
				break;
			}
			continue;
		}

		/* Extract the topic, timestamp and value from the per-value map. */
		CborValue vto;
		CborValue vts;
		CborValue vv;
		char topic[NBUS_MQ_CLIENT_MAX_TOPIC_LEN] = {0};
		size_t topic_len = sizeof(topic);
		time_t ts_sec = 0;
		float value = 0.0f;

		bool ok = cbor_value_map_find_value(&it, "to", &vto) == CborNoError && cbor_value_is_text_string(&vto) &&
		          cbor_value_map_find_value(&it, "v", &vv) == CborNoError && cbor_value_is_float(&vv);

		if (ok) {
			cbor_value_copy_text_string(&vto, topic, &topic_len, NULL);
			topic[sizeof(topic) - 1] = '\0';
			cbor_value_get_float(&vv, &value);

			/* The timestamp is optional. */
			if (cbor_value_map_find_value(&it, "ts", &vts) == CborNoError && cbor_value_is_unsigned_integer(&vts)) {
				uint64_t u = 0;
				cbor_value_get_uint64(&vts, &u);
				ts_sec = (time_t)u;
			}

			publish_value(self, topic, value, ts_sec);
		}

		/* Advance past the whole per-value map to the next array element. */
		if (cbor_value_advance(&it) != CborNoError) {
			break;
		}
	}
}


/**********************************************************************************************************************
 * Polling tasks
 **********************************************************************************************************************/

/* Periodically send a poll request to the remote service. The remote answers with the oldest ready
 * batch (or an empty CBOR map), so an empty map is enough as the request. Sending does not wait for
 * the response, so a lost request or response never stalls the exchange. */
static void send_task(void *p) {
	NbusMqClient *self = p;

	self->send_running = true;
	while (self->can_run) {
		self->conf.d->vmt->write(self->conf.d, "\xbf\xff", 2, NULL);
		vTaskDelay(pdMS_TO_TICKS(self->conf.poll_interval_ms));
	}
	self->send_running = false;

	vTaskDelete(NULL);
}


/* Receive the response batches as they arrive and republish every value they carry. */
static void recv_task(void *p) {
	NbusMqClient *self = p;

	self->recv_running = true;
	while (self->can_run) {
		size_t len = NBUS_MQ_CLIENT_RX_BUF_LEN;
		if (self->conf.d->vmt->read(self->conf.d, self->rx_buf, &len, NULL) == DATAGRAM_RET_OK) {
			process_batch(self, self->rx_buf, len);
		}
	}
	self->recv_running = false;

	vTaskDelete(NULL);
}


/**********************************************************************************************************************
 * Service lifecycle
 **********************************************************************************************************************/

nbus_mq_client_ret_t nbus_mq_client_init(NbusMqClient *self, const struct nbus_mq_client_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return NBUS_MQ_CLIENT_RET_NULL;
	}
	memset(self, 0, sizeof(NbusMqClient));
	memcpy(&self->conf, conf, sizeof(struct nbus_mq_client_conf));

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return NBUS_MQ_CLIENT_RET_OK;
}


nbus_mq_client_ret_t nbus_mq_client_free(NbusMqClient *self) {
	if (u_assert(self != NULL)) {
		return NBUS_MQ_CLIENT_RET_FAILED;
	}

	return NBUS_MQ_CLIENT_RET_OK;
}


nbus_mq_client_ret_t nbus_mq_client_start(NbusMqClient *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->conf.mq != NULL) ||
	    u_assert(self->conf.d != NULL) ||
	    u_assert(self->conf.poll_interval_ms > 0)) {
		return NBUS_MQ_CLIENT_RET_FAILED;
	}

	if (self->conf.topic_prefix != NULL) {
		strlcpy(self->topic_prefix, self->conf.topic_prefix, NBUS_MQ_CLIENT_MAX_PREFIX_LEN);
	}

	/* Open a message queue client to republish the received values. */
	self->mqc = self->conf.mq->vmt->open(self->conf.mq);
	if (self->mqc == NULL) {
		goto err;
	}

	self->can_run = true;
	xTaskCreate(recv_task, "nbus-mq-cl-rx", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->recv_task));
	if (self->recv_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create receiving task"));
		goto err;
	}

	xTaskCreate(send_task, "nbus-mq-cl-tx", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->send_task));
	if (self->send_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create sending task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("started, polling every %lu ms"), self->conf.poll_interval_ms);
	return NBUS_MQ_CLIENT_RET_OK;
err:
	/* Not fully started, stop everything. */
	nbus_mq_client_stop(self);
	return NBUS_MQ_CLIENT_RET_FAILED;
}


nbus_mq_client_ret_t nbus_mq_client_stop(NbusMqClient *self) {
	if (u_assert(self != NULL)) {
		return NBUS_MQ_CLIENT_RET_FAILED;
	}

	/* Stop the sending task. The receiving task may stay blocked in a datagram read until the next
	 * datagram arrives; it exits on the following loop check. */
	self->can_run = false;
	while (self->send_running) {
		vTaskDelay(100);
	}

	if (self->mqc != NULL) {
		self->mqc->vmt->close(self->mqc);
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return NBUS_MQ_CLIENT_RET_OK;
}
