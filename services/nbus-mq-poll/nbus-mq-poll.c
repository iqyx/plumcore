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
 * Bridges the message queue to an nbus2 endpoint using a poll model over a best-effort (UDP-like)
 * datagram transport. Values received on the configured topic are serialised into CBOR as they
 * arrive and accumulated into fixed-size batches. Each sealed batch is stamped with a monotonic
 * sequence number and retained until the client acknowledges it, which turns a lost request or reply
 * into a harmless retransmission instead of lost data.
 *
 * Wire protocol (all CBOR):
 *
 *   request:  { "a": <ack_seq>, "m": <max_batches> }
 *       "a" is the highest sequence number the client has durably received; the server retires every
 *       retained batch up to and including it. "m" caps how many batches the reply may carry.
 *
 *   reply:    { "h": <device>, "p": <pending>, "q": <buffering>, "b": [ <batch>, <batch>, ... ] }
 *       "b" holds the lowest-sequence retained batches not yet acknowledged, in ascending order and
 *       bounded by both "m" and the reply buffer size. "p" is how many further ready batches remain
 *       after this reply (a backlog hint for the client to pace its polling); "q" is how many
 *       messages sit in the not-yet-sealed batch. Both are hints valid only at reply time.
 *
 *   batch:    { "d": [ {"ts","tn","to","v"}, ... ], "s": <seq> }
 *       A self-contained, pre-serialised unit. "s" is its sequence number; "d" the carried values.
 *
 * The request is idempotent: it is a pure function of the acknowledged cursor, so the server keeps no
 * per-client connection state and every retained batch is byte-identical across retransmissions. The
 * reception side (fills batches) and the request-servicing side (seals, sends and retires batches)
 * run as two independent tasks sharing the batch ring under a mutex.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
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

/* Bytes reserved past the current payload for sealing a batch: the "s" sequence key (1) plus its
 * uint32 value (up to 5), the array break (1) and the map break (1). */
#define NBUS_MQ_POLL_SEAL_RESERVE 9


static nbus_mq_poll_ret_t prepare_buffer(NbusMqPoll *self, struct nbus_mq_poll_msg_buffer *buf) {
	(void)self;
	buf->state = NBUS_MQ_POLL_MB_STATE_ACTIVE;
	buf->len = 0;
	buf->count = 0;

	/* Open the top level batch map and its "d" values array. The device name is not repeated per
	 * batch; it is carried once in the reply envelope. */
	buf->data[buf->len++] = 0xbf;
	buf->data[buf->len++] = 0x61;
	buf->data[buf->len++] = 'd';
	buf->data[buf->len++] = 0x9f;
	return NBUS_MQ_POLL_RET_OK;
}


static nbus_mq_poll_ret_t close_buffer(NbusMqPoll *self, struct nbus_mq_poll_msg_buffer *buf) {
	/* Close the values array container. */
	buf->data[buf->len++] = 0xff;

	/* Stamp the batch with the next sequence number. */
	buf->seq = self->next_seq++;
	uint8_t cbor[8];
	CborEncoder encoder;
	cbor_encoder_init(&encoder, cbor, sizeof(cbor), 0);
	cbor_encode_text_stringz(&encoder, "s");
	cbor_encode_uint(&encoder, buf->seq);
	size_t cbor_len = cbor_encoder_get_buffer_size(&encoder, cbor);
	memcpy(buf->data + buf->len, cbor, cbor_len);
	buf->len += cbor_len;

	/* Close the top level map container. */
	buf->data[buf->len++] = 0xff;

	buf->state = NBUS_MQ_POLL_MB_STATE_FULL;
	return NBUS_MQ_POLL_RET_OK;
}


/* Number of retained (sealed, not yet acknowledged) batches. */
static size_t count_full_buffers(NbusMqPoll *self) {
	size_t n = 0;
	for (size_t i = 0; i < NBUS_MQ_POLL_MSG_BUFFERS; i++) {
		if (self->msg_buffers[i].state == NBUS_MQ_POLL_MB_STATE_FULL) {
			n++;
		}
	}
	return n;
}


/* Lowest-sequence retained batch with a sequence number strictly above above, or NULL. Serial-number
 * arithmetic keeps this correct across the 32-bit sequence wrap. Used to send batches in order,
 * passing the acknowledged cursor as above. */
static struct nbus_mq_poll_msg_buffer *find_next_full(NbusMqPoll *self, uint32_t above) {
	struct nbus_mq_poll_msg_buffer *best = NULL;
	for (size_t i = 0; i < NBUS_MQ_POLL_MSG_BUFFERS; i++) {
		struct nbus_mq_poll_msg_buffer *b = &self->msg_buffers[i];
		if (b->state != NBUS_MQ_POLL_MB_STATE_FULL || (int32_t)(b->seq - above) <= 0) {
			continue;
		}
		if (best == NULL || (int32_t)(b->seq - best->seq) < 0) {
			best = b;
		}
	}
	return best;
}


/* Oldest retained batch by serial sequence order, or NULL when none are retained. */
static struct nbus_mq_poll_msg_buffer *find_oldest_full(NbusMqPoll *self) {
	struct nbus_mq_poll_msg_buffer *best = NULL;
	for (size_t i = 0; i < NBUS_MQ_POLL_MSG_BUFFERS; i++) {
		struct nbus_mq_poll_msg_buffer *b = &self->msg_buffers[i];
		if (b->state != NBUS_MQ_POLL_MB_STATE_FULL) {
			continue;
		}
		if (best == NULL || (int32_t)(b->seq - best->seq) < 0) {
			best = b;
		}
	}
	return best;
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

	/* The retention window is exhausted: every batch is sealed and still unacknowledged. Evict the
	 * oldest one so reception can continue. Its loss is not silent, the client observes the resulting
	 * jump in sequence numbers as a gap. */
	struct nbus_mq_poll_msg_buffer *victim = find_oldest_full(self);
	if (victim != NULL) {
		prepare_buffer(self, victim);
		return victim;
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
	cbor_encode_text_stringz(&encoder, "tn");
	cbor_encode_uint(&encoder, ts->tv_nsec);
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

	xSemaphoreTake(self->lock, portMAX_DELAY);

	struct nbus_mq_poll_msg_buffer *b = get_active_buffer(self);
	if (b == NULL) {
		xSemaphoreGive(self->lock);
		return NBUS_MQ_POLL_RET_FAILED;
	}

	/* Reserve room for the value map delimiters (2 bytes) and for sealing the batch. When the current
	 * batch cannot hold the value, seal it and start a fresh one. */
	if ((2 + cbor_len + NBUS_MQ_POLL_SEAL_RESERVE + b->len) > NBUS_MQ_POLL_MSG_BUFFER_SIZE) {
		close_buffer(self, b);
		b = get_active_buffer(self);
	}
	if (b == NULL) {
		xSemaphoreGive(self->lock);
		return NBUS_MQ_POLL_RET_FAILED;
	}

	/* Wrap the serialised value in its own map inside the values array. */
	b->data[b->len++] = 0xbf;
	memcpy(b->data + b->len, cbor, cbor_len);
	b->len += cbor_len;
	b->data[b->len++] = 0xff;
	b->count++;

	xSemaphoreGive(self->lock);
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

/* Parse a poll request into the acknowledged cursor and the maximum number of batches to return. A
 * malformed or empty request degrades gracefully to "acknowledge nothing, return the default max". */
static void parse_request(NbusMqPoll *self, const uint8_t *buf, size_t len, uint32_t *ack, size_t *max) {
	*ack = 0;
	*max = NBUS_MQ_POLL_MSG_BUFFERS;

	CborParser parser;
	CborValue root;
	if (cbor_parser_init(buf, len, 0, &parser, &root) != CborNoError || !cbor_value_is_map(&root)) {
		return;
	}

	CborValue v;
	if (cbor_value_map_find_value(&root, "a", &v) == CborNoError && cbor_value_is_unsigned_integer(&v)) {
		uint64_t u = 0;
		cbor_value_get_uint64(&v, &u);
		*ack = (uint32_t)u;
	}
	if (cbor_value_map_find_value(&root, "m", &v) == CborNoError && cbor_value_is_unsigned_integer(&v)) {
		uint64_t u = 0;
		cbor_value_get_uint64(&v, &u);
		if (u >= 1 && u <= NBUS_MQ_POLL_MSG_BUFFERS) {
			*max = (size_t)u;
		}
	}
}


/* Assemble the reply to a poll request into self->tx_buf and return its length. Retires every batch
 * up to the acknowledged cursor, flushes a pending partial batch when nothing sealed is waiting, then
 * packs the lowest-sequence retained batches (bounded by max and the buffer size) into the reply. */
static size_t build_reply(NbusMqPoll *self, uint32_t ack, size_t max) {
	xSemaphoreTake(self->lock, portMAX_DELAY);

	/* Retire every retained batch the client has acknowledged. */
	for (size_t i = 0; i < NBUS_MQ_POLL_MSG_BUFFERS; i++) {
		struct nbus_mq_poll_msg_buffer *b = &self->msg_buffers[i];
		if (b->state == NBUS_MQ_POLL_MB_STATE_FULL && (int32_t)(b->seq - ack) <= 0) {
			b->state = NBUS_MQ_POLL_MB_STATE_EMPTY;
		}
	}

	/* Flush the tail: when no sealed batch is waiting, seal the partial one so the last few values are
	 * delivered promptly instead of lingering until the batch happens to fill. */
	if (count_full_buffers(self) == 0) {
		for (size_t i = 0; i < NBUS_MQ_POLL_MSG_BUFFERS; i++) {
			if (self->msg_buffers[i].state == NBUS_MQ_POLL_MB_STATE_ACTIVE && self->msg_buffers[i].count > 0) {
				close_buffer(self, &self->msg_buffers[i]);
				break;
			}
		}
	}

	/* Select the batches to send: lowest sequence first, bounded by the client maximum and by what the
	 * reply buffer can hold alongside the envelope. */
	size_t overhead = 1 + (2 + 1 + strlen(self->conf.device_name)) + (2 + 5) + (2 + 5) + 2 + 1 + 2;
	struct nbus_mq_poll_msg_buffer *sel[NBUS_MQ_POLL_MSG_BUFFERS];
	size_t nsel = 0;
	size_t used = overhead;
	uint32_t last = ack;
	while (nsel < max) {
		struct nbus_mq_poll_msg_buffer *b = find_next_full(self, last);
		if (b == NULL || (used + b->len) > NBUS_MQ_POLL_TX_BUF_LEN) {
			break;
		}
		sel[nsel++] = b;
		used += b->len;
		last = b->seq;
	}

	/* Backlog hints: batches still ready after this reply, and messages sitting in the partial one. */
	size_t pending = count_full_buffers(self) - nsel;
	size_t buffering = 0;
	for (size_t i = 0; i < NBUS_MQ_POLL_MSG_BUFFERS; i++) {
		if (self->msg_buffers[i].state == NBUS_MQ_POLL_MB_STATE_ACTIVE) {
			buffering = self->msg_buffers[i].count;
			break;
		}
	}

	/* Encode the reply envelope header { "h", "p", "q", "b": [ ... Sized for the device name plus the
	 * two small hint integers and the fixed keys. */
	uint8_t hdr[128];
	CborEncoder encoder;
	cbor_encoder_init(&encoder, hdr, sizeof(hdr), 0);
	cbor_encode_text_stringz(&encoder, "h");
	cbor_encode_text_stringz(&encoder, self->conf.device_name);
	cbor_encode_text_stringz(&encoder, "p");
	cbor_encode_uint(&encoder, pending);
	cbor_encode_text_stringz(&encoder, "q");
	cbor_encode_uint(&encoder, buffering);
	cbor_encode_text_stringz(&encoder, "b");
	size_t hdr_len = cbor_encoder_get_buffer_size(&encoder, hdr);

	size_t pos = 0;
	self->tx_buf[pos++] = 0xbf;
	memcpy(self->tx_buf + pos, hdr, hdr_len);
	pos += hdr_len;
	self->tx_buf[pos++] = 0x9f;
	for (size_t i = 0; i < nsel; i++) {
		memcpy(self->tx_buf + pos, sel[i]->data, sel[i]->len);
		pos += sel[i]->len;
	}
	self->tx_buf[pos++] = 0xff;
	self->tx_buf[pos++] = 0xff;

	xSemaphoreGive(self->lock);
	return pos;
}


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

		uint32_t ack = 0;
		size_t max = 0;
		parse_request(self, self->nbus_buf, len, &ack, &max);

		/* Retained batches are copied into the private tx buffer under the lock, so the reply can be
		 * sent without holding it (the datagram write may block) and without racing the reception task. */
		size_t txlen = build_reply(self, ack, max);

		/* Answer to the requester's address. */
		struct datagram_msg txmsg = {0};
		txmsg.addr_size = 4;
		txmsg.dst_port = rxmsg.src_port;
		memcpy(txmsg.dst_addr, rxmsg.src_addr, 4);
		self->conf.d->vmt->write(self->conf.d, self->tx_buf, txlen, &txmsg);
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

	/* Sequence 0 is reserved for the client cursor meaning "nothing received yet", so the first sealed
	 * batch is numbered 1. */
	self->next_seq = 1;

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

	/* Guards the batch ring shared between the reception and request-servicing tasks. */
	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		goto err;
	}

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

	if (self->lock != NULL) {
		vSemaphoreDelete(self->lock);
		self->lock = NULL;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return NBUS_MQ_POLL_RET_OK;
}
