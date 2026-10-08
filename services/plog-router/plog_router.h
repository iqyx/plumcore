/* SPDX-License-Identifier: BSD-2-Clause
 *
 * plog message queue router
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <FreeRTOS.h>
#include <semphr.h>

#include <interfaces/mq.h>
#include <interfaces/clock.h>


#define PLOG_ROUTER_TOPIC_LEN_MAX 64
#define PLOG_ROUTER_FILTERS_MAX 8
#define PLOG_ROUTER_RX_TIMEOUT_MS_DEFAULT 500
/* Upper bound on how long a delivery waits for the receiving client to acknowledge a message. It
 * caps the time a publisher can be blocked by a client whose receiver has stopped but which has not
 * been closed yet, so such a client can never wedge the publisher permanently. */
#define PLOG_ROUTER_DELIVER_TIMEOUT_MS 1000

typedef enum {
	PLOG_ROUTER_RET_OK = 0,
	PLOG_ROUTER_RET_FAILED,
	PLOG_ROUTER_RET_NULL,
	PLOG_ROUTER_RET_BAD_ARG,
} plog_router_ret_t;

struct plog_router_msg_send {
	const struct ndarray *array;
	const struct timespec *ts;
	const char *topic;
};

struct plog_router_msg_recv {
	plog_router_ret_t ret;
};

struct plog_router_mq_client {
	MqClient client;
	/* A client may subscribe to multiple topic filters at once. A message is delivered if it
	 * matches any of them. Empty slots hold an empty string. */
	char topic_filters[PLOG_ROUTER_FILTERS_MAX][PLOG_ROUTER_TOPIC_LEN_MAX];
	uint32_t rx_timeout_ms;

	SemaphoreHandle_t msg_mutex;
	QueueHandle_t send_lock;
	QueueHandle_t recv_lock;

	/* Number of publishers currently delivering to this client outside of the clients_mutex. The client
	 * stays linked (and its next pointer valid) while non-zero. Guarded by clients_mutex. */
	uint32_t refs;
	/* Set by close(), no new deliveries are started to a closing client. Guarded by clients_mutex. */
	bool closing;
};

typedef struct {
	/* RTC clock service dependency. It is used to timestamp
	 * all messages without a valid time set. It is discovered
	 * in runtime using the service locator. */
	Clock *rtc;

	/* The service implements a Mq interface. */
	Mq mq;

	struct plog_router_mq_client *first_client;
	/* Guards the client list and the per-client refs/closing fields. It is not held during a delivery,
	 * a publisher pins the client with a reference instead. Holding it across a delivery would deadlock
	 * a client publishing from its receive loop against the next delivery to it. */
	SemaphoreHandle_t clients_mutex;

	bool initialized;
	bool debug;

} PlogRouter;

/* PlogRouter API */
plog_router_ret_t plog_router_init(PlogRouter *self);
plog_router_ret_t plog_router_free(PlogRouter *self);
plog_router_ret_t plog_router_set_clock(PlogRouter *self, Clock *rtc);

