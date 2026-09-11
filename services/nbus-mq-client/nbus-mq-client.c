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
 * Client counterpart to the nbus-mq-poll service. It polls a remote nbus-mq-poll endpoint over the
 * best-effort nbus2 transport, receives one or more serialised batches in response, decodes them and
 * republishes every carried value to a local message queue.
 *
 * Each poll request carries { "a": <cursor>, "m": <max_batches> }: the cursor is the highest batch
 * sequence number received contiguously so far, acknowledging everything up to it, and max_batches
 * caps the reply size. The reply is { "h", "p", "q", "b": [ { "d":[...], "s":<seq> }, ... ] }; every
 * value carries a timestamp ("ts"/"tn"), the original topic ("to") and a float ("v").
 *
 * Because the request is a pure function of the cursor, a lost request or reply simply causes the
 * same batches to be re-sent on the next poll: batches are deduplicated and ordered by sequence
 * number, so nothing is lost or applied twice. The backlog hint "p" is used to drain a backlog back
 * to back and fall back to the idle interval only once the remote reports nothing pending.
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
static void publish_value(NbusMqClient *self, const char *topic, float value, time_t ts_sec, long ts_nsec) {
	/* Sized to hold "<prefix>/<topic>" plus the separator and terminator without truncation. */
	char full[NBUS_MQ_CLIENT_MAX_PREFIX_LEN + NBUS_MQ_CLIENT_MAX_TOPIC_LEN + 2] = {0};
	if (self->topic_prefix[0] != '\0') {
		snprintf(full, sizeof(full), "%s/%s", self->topic_prefix, topic);
	} else {
		strlcpy(full, topic, sizeof(full));
	}

	struct timespec ts = {0};
	ts.tv_sec = ts_sec;
	ts.tv_nsec = ts_nsec;
	NdArray array;
	ndarray_init_view(&array, DTYPE_FLOAT, 1, &value, sizeof(value));
	self->mqc->vmt->publish(self->mqc, full, &array, &ts);
}


/* Republish every value carried by a single decoded batch map. */
static void publish_batch(NbusMqClient *self, const CborValue *batch) {
	/* Locate the "d" values array. */
	CborValue array;
	if (cbor_value_map_find_value(batch, "d", &array) != CborNoError || !cbor_value_is_array(&array)) {
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
		long ts_nsec = 0;
		float value = 0.0f;

		bool ok = cbor_value_map_find_value(&it, "to", &vto) == CborNoError && cbor_value_is_text_string(&vto) &&
		          cbor_value_map_find_value(&it, "v", &vv) == CborNoError && cbor_value_is_float(&vv);

		if (ok) {
			cbor_value_copy_text_string(&vto, topic, &topic_len, NULL);
			topic[sizeof(topic) - 1] = '\0';
			cbor_value_get_float(&vv, &value);

			/* The timestamp is optional; seconds under "ts", nanoseconds under "tn". */
			if (cbor_value_map_find_value(&it, "ts", &vts) == CborNoError && cbor_value_is_unsigned_integer(&vts)) {
				uint64_t u = 0;
				cbor_value_get_uint64(&vts, &u);
				ts_sec = (time_t)u;
			}
			if (cbor_value_map_find_value(&it, "tn", &vts) == CborNoError && cbor_value_is_unsigned_integer(&vts)) {
				uint64_t u = 0;
				cbor_value_get_uint64(&vts, &u);
				ts_nsec = (long)u;
			}

			publish_value(self, topic, value, ts_sec, ts_nsec);
		}

		/* Advance past the whole per-value map to the next array element. */
		if (cbor_value_advance(&it) != CborNoError) {
			break;
		}
	}
}


/* Decode a received reply, republishing the values of every not-yet-seen batch it carries and
 * advancing the acknowledgement cursor. Returns the number of batches the remote still has pending
 * after this reply, so the caller can drain a backlog without waiting the idle interval. */
static size_t process_reply(NbusMqClient *self, const uint8_t *buf, size_t len) {
	CborParser parser;
	CborValue root;
	if (cbor_parser_init(buf, len, 0, &parser, &root) != CborNoError || !cbor_value_is_map(&root)) {
		return 0;
	}

	/* Backlog hint: how many ready batches remain after this reply. */
	size_t pending = 0;
	CborValue vp;
	if (cbor_value_map_find_value(&root, "p", &vp) == CborNoError && cbor_value_is_unsigned_integer(&vp)) {
		uint64_t u = 0;
		cbor_value_get_uint64(&vp, &u);
		pending = (size_t)u;
	}

	/* Locate the "b" batches array. Absent when nothing was returned. */
	CborValue array;
	if (cbor_value_map_find_value(&root, "b", &array) != CborNoError || !cbor_value_is_array(&array)) {
		return pending;
	}

	CborValue it;
	if (cbor_value_enter_container(&array, &it) != CborNoError) {
		return pending;
	}

	while (!cbor_value_at_end(&it)) {
		if (!cbor_value_is_map(&it)) {
			if (cbor_value_advance(&it) != CborNoError) {
				break;
			}
			continue;
		}

		/* A batch without a sequence number cannot be ordered or deduplicated, skip it. */
		CborValue vs;
		if (cbor_value_map_find_value(&it, "s", &vs) == CborNoError && cbor_value_is_unsigned_integer(&vs)) {
			uint64_t u = 0;
			cbor_value_get_uint64(&vs, &u);
			uint32_t seq = (uint32_t)u;

			if ((int32_t)(seq - self->cursor) <= 0) {
				/* Already applied (a retransmission of an unacknowledged batch), ignore its values. */
			} else {
				/* A jump past the next expected sequence means the remote evicted batches we never
				 * received. Report the gap; the lost values cannot be recovered. */
				if ((int32_t)(seq - (self->cursor + 1)) > 0) {
					u_log(system_log, LOG_TYPE_WARN,
					      U_LOG_MODULE_PREFIX("sequence gap, missed %lu batch(es) before seq %lu"),
					      (unsigned long)(seq - self->cursor - 1), (unsigned long)seq);
				}
				publish_batch(self, &it);
				self->cursor = seq;
			}
		}

		if (cbor_value_advance(&it) != CborNoError) {
			break;
		}
	}

	return pending;
}


/**********************************************************************************************************************
 * Polling tasks
 **********************************************************************************************************************/

/* Periodically send a poll request carrying the acknowledgement cursor and the batch limit. Sending
 * does not wait for the response, so a lost request or response never stalls the exchange; the next
 * request simply re-acknowledges the same cursor. The idle interval paces polling, but the receiver
 * notifies this task to poll again immediately whenever the remote still has a backlog pending. */
static void send_task(void *p) {
	NbusMqClient *self = p;
	uint32_t max = self->conf.max_batches ? self->conf.max_batches : NBUS_MQ_CLIENT_DEFAULT_MAX_BATCHES;

	self->send_running = true;
	while (self->can_run) {
		uint8_t req[24];
		CborEncoder encoder;
		CborEncoder map;
		cbor_encoder_init(&encoder, req, sizeof(req), 0);
		cbor_encoder_create_map(&encoder, &map, 2);
		cbor_encode_text_stringz(&map, "a");
		cbor_encode_uint(&map, self->cursor);
		cbor_encode_text_stringz(&map, "m");
		cbor_encode_uint(&map, max);
		cbor_encoder_close_container(&encoder, &map);
		self->conf.d->vmt->write(self->conf.d, req, cbor_encoder_get_buffer_size(&encoder, req), NULL);

		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(self->conf.poll_interval_ms));
	}
	self->send_running = false;

	vTaskDelete(NULL);
}


/* Receive the reply datagrams as they arrive and republish every value they carry. When the remote
 * reports a remaining backlog, wake the sender so the next poll goes out without the idle wait. */
static void recv_task(void *p) {
	NbusMqClient *self = p;

	self->recv_running = true;
	while (self->can_run) {
		size_t len = NBUS_MQ_CLIENT_RX_BUF_LEN;
		if (self->conf.d->vmt->read(self->conf.d, self->rx_buf, &len, NULL) == DATAGRAM_RET_OK) {
			if (process_reply(self, self->rx_buf, len) > 0 && self->send_task != NULL) {
				xTaskNotifyGive(self->send_task);
			}
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
