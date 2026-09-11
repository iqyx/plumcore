/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NBUS2 message queue poll client service
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

#include <interfaces/mq.h>
#include <interfaces/datagram.h>
#include <types/ndarray.h>


/* Scratch buffer for a single received reply datagram. */
#define NBUS_MQ_CLIENT_RX_BUF_LEN 256
#define NBUS_MQ_CLIENT_MAX_TOPIC_LEN 32
/* Length of the prefix prepended to every republished topic (see topic_prefix). */
#define NBUS_MQ_CLIENT_MAX_PREFIX_LEN 32
/* Number of batches requested per poll when the configuration leaves max_batches at 0. */
#define NBUS_MQ_CLIENT_DEFAULT_MAX_BATCHES 4


typedef enum {
	NBUS_MQ_CLIENT_RET_OK = 0,
	NBUS_MQ_CLIENT_RET_FAILED,
	NBUS_MQ_CLIENT_RET_NULL,
} nbus_mq_client_ret_t;


/* Service configuration passed to nbus_mq_client_init(). Not typedef'd per project policy. */
struct nbus_mq_client_conf {
	/** Message queue the received values are published to. */
	Mq *mq;
	/** Datagram (a bound and connected nbus2 socket) talking to the remote nbus-mq-poll service. */
	Datagram *d;
	/** Idle interval between two poll requests in milliseconds. A backlog is drained back to back
	 *  regardless of this, which only paces polling once the remote reports nothing pending. */
	uint32_t poll_interval_ms;
	/** Maximum number of batches to request per poll. 0 selects the built-in default. */
	uint32_t max_batches;
	/** Optional prefix prepended as "<prefix>/<topic>" to every republished topic. NULL or "" to
	 *  publish under the remote topic unchanged. */
	const char *topic_prefix;
};


typedef struct {
	struct nbus_mq_client_conf conf;

	char topic_prefix[NBUS_MQ_CLIENT_MAX_PREFIX_LEN];

	/* MQ client the decoded values are published through. */
	MqClient *mqc;

	/* Highest batch sequence number received contiguously so far, echoed as the acknowledged cursor in
	 * every poll request. 0 means nothing received yet. Written by the receiver, read by the sender; a
	 * 32-bit access is atomic on the target so no lock is needed. */
	volatile uint32_t cursor;

	/* Scratch buffer for a single received reply. */
	uint8_t rx_buf[NBUS_MQ_CLIENT_RX_BUF_LEN];

	/* Sending and receiving run as two independent tasks: the sender keeps polling at a fixed rate
	 * regardless of whether responses arrive, and the receiver processes whatever comes back. This
	 * keeps a single lost request or response from stalling the exchange. */
	volatile bool can_run;
	volatile bool send_running;
	volatile bool recv_running;
	TaskHandle_t send_task;
	TaskHandle_t recv_task;
} NbusMqClient;


/**
 * @brief Initialize the NBUS message queue poll client
 *
 * Copies the configuration into the instance. Resources are allocated later in
 * nbus_mq_client_start().
 *
 * @param self Preallocated instance.
 * @param conf Service configuration. The structure is copied.
 *
 * @return NBUS_MQ_CLIENT_RET_NULL on a NULL argument, NBUS_MQ_CLIENT_RET_OK otherwise.
 */
nbus_mq_client_ret_t nbus_mq_client_init(NbusMqClient *self, const struct nbus_mq_client_conf *conf);

nbus_mq_client_ret_t nbus_mq_client_free(NbusMqClient *self);

/**
 * @brief Start polling the remote service and republishing received values
 *
 * Opens a message queue client and starts the polling task. The task periodically requests a batch
 * from the remote nbus-mq-poll service, decodes it and publishes every carried value to the message
 * queue.
 *
 * @return NBUS_MQ_CLIENT_RET_FAILED on error, NBUS_MQ_CLIENT_RET_OK otherwise.
 */
nbus_mq_client_ret_t nbus_mq_client_start(NbusMqClient *self);

nbus_mq_client_ret_t nbus_mq_client_stop(NbusMqClient *self);
