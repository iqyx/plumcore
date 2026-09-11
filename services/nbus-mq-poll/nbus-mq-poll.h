/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NBUS2 message queue poll bridge service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <interfaces/mq.h>
#include <interfaces/datagram.h>
#include <types/ndarray.h>


/* Maximum size of a single batch of serialised messages (in bytes). */
#define NBUS_MQ_POLL_MSG_BUFFER_SIZE 128
/* Number of batches kept in the ring. A sealed batch is retained here until the client acknowledges
 * it, so this doubles as the retransmission/retention window. When the window is exhausted the oldest
 * unacknowledged batch is evicted and its loss becomes visible to the client as a sequence gap. */
#define NBUS_MQ_POLL_MSG_BUFFERS 4
#define NBUS_MQ_POLL_MAX_TOPIC_LEN 32
/* Scratch buffer for a single received poll-request datagram. */
#define NBUS_MQ_POLL_NBUS_BUF_LEN 256
/* Buffer holding a single assembled poll reply. Bounded by the client receive buffer; it caps how
 * many retained batches can be packed into one reply regardless of the client requested maximum. */
#define NBUS_MQ_POLL_TX_BUF_LEN 256


typedef enum {
	NBUS_MQ_POLL_RET_OK = 0,
	NBUS_MQ_POLL_RET_FAILED,
	NBUS_MQ_POLL_RET_NULL,
} nbus_mq_poll_ret_t;


enum nbus_mq_poll_msg_buffer_state {
	NBUS_MQ_POLL_MB_STATE_EMPTY = 0,
	NBUS_MQ_POLL_MB_STATE_ACTIVE,
	NBUS_MQ_POLL_MB_STATE_FULL,
};

struct nbus_mq_poll_msg_buffer {
	enum nbus_mq_poll_msg_buffer_state state;
	uint8_t data[NBUS_MQ_POLL_MSG_BUFFER_SIZE];
	size_t len;
	/* Number of serialised messages carried by the batch. */
	size_t count;
	/* Monotonic sequence number assigned when the batch is sealed. A retained batch keeps its
	 * sequence number until the client acknowledges it; the value drives ordering, deduplication and
	 * gap detection on the client. Only meaningful in the FULL state. */
	uint32_t seq;
};


/* Service configuration passed to nbus_mq_poll_init(). Not typedef'd per project policy. */
struct nbus_mq_poll_conf {
	/** Message queue the service subscribes to. */
	Mq *mq;
	/** Datagram (a bound nbus2 socket) the poll requests are received on and answered to. */
	Datagram *d;
	/** Topic subscribed to; matching values are serialised and batched for polling. */
	const char *topic;
	/** Device name emitted in the "h" header field of every batch. */
	const char *device_name;
};


typedef struct {
	struct nbus_mq_poll_conf conf;

	char topic[NBUS_MQ_POLL_MAX_TOPIC_LEN];

	/* NBUS request/response side: receives poll requests and answers with a ready batch. */
	uint8_t nbus_buf[NBUS_MQ_POLL_NBUS_BUF_LEN];
	uint8_t tx_buf[NBUS_MQ_POLL_TX_BUF_LEN];
	volatile bool nbus_can_run;
	volatile bool nbus_running;
	TaskHandle_t nbus_task;

	/* Batches of serialised messages waiting to be polled. Shared between the reception task (which
	 * fills them) and the request task (which seals, sends and retires them), guarded by lock. */
	struct nbus_mq_poll_msg_buffer msg_buffers[NBUS_MQ_POLL_MSG_BUFFERS];
	/* Sequence number to assign to the next sealed batch. */
	uint32_t next_seq;
	SemaphoreHandle_t lock;

	/* Message queue reception side. */
	MqClient *mqc;
	NdArray rx_buf;
	volatile bool rx_can_run;
	volatile bool rx_running;
	TaskHandle_t rx_task;
} NbusMqPoll;


/**
 * @brief Initialize the NBUS message queue poll bridge
 *
 * Copies the configuration into the instance. Resources are allocated later in
 * nbus_mq_poll_start().
 *
 * @param self Preallocated instance.
 * @param conf Service configuration. The structure is copied.
 *
 * @return NBUS_MQ_POLL_RET_NULL on a NULL argument, NBUS_MQ_POLL_RET_OK otherwise.
 */
nbus_mq_poll_ret_t nbus_mq_poll_init(NbusMqPoll *self, const struct nbus_mq_poll_conf *conf);

nbus_mq_poll_ret_t nbus_mq_poll_free(NbusMqPoll *self);

/**
 * @brief Start receiving from the message queue and answering poll requests
 *
 * Opens a message queue client, subscribes to the configured topic and starts the reception and
 * NBUS servicing tasks.
 *
 * @return NBUS_MQ_POLL_RET_FAILED on error, NBUS_MQ_POLL_RET_OK otherwise.
 */
nbus_mq_poll_ret_t nbus_mq_poll_start(NbusMqPoll *self);

nbus_mq_poll_ret_t nbus_mq_poll_stop(NbusMqPoll *self);
