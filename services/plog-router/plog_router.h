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
#define PLOG_ROUTER_RX_TIMEOUT_MS_DEFAULT 500

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
	/* We support one topic filter per client so far. */
	char topic_filter[PLOG_ROUTER_TOPIC_LEN_MAX];
	uint32_t rx_timeout_ms;

	SemaphoreHandle_t msg_mutex;
	QueueHandle_t send_lock;
	QueueHandle_t recv_lock;
	

};

typedef struct {
	/* RTC clock service dependency. It is used to timestamp
	 * all messages without a valid time set. It is discovered
	 * in runtime using the service locator. */
	Clock *rtc;

	/* The service implements a Mq interface. */
	Mq mq;

	struct plog_router_mq_client *first_client;

	bool initialized;
	bool debug;

} PlogRouter;

/* PlogRouter API */
plog_router_ret_t plog_router_init(PlogRouter *self);
plog_router_ret_t plog_router_free(PlogRouter *self);
plog_router_ret_t plog_router_set_clock(PlogRouter *self, Clock *rtc);

