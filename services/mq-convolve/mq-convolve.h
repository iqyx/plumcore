/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MQ moving-window service
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
#include <types/ndarray.h>

#define MQ_CONVOLVE_MAX_TOPIC_LEN 32
#define MQ_CONVOLVE_RXBUF_SAMPLES 32

typedef enum {
	MQ_CONVOLVE_RET_OK = 0,
	MQ_CONVOLVE_RET_FAILED,
	MQ_CONVOLVE_RET_NULL,
} mq_convolve_ret_t;

/* Service configuration passed to mq_convolve_init(). Not typedef'd per project policy. */
struct mq_convolve_conf {
	/** Message queue the service receives from and publishes to. */
	Mq *mq;
	/** Moving window size in values. The published array always has this many elements. */
	size_t window;
	/** Number of newly received values between two consecutive publishes. */
	size_t step;
	/** Topic to subscribe to for incoming values. */
	const char *sub_topic;
	/** Topic the moving window is published back to. */
	const char *pub_topic;
};

typedef struct {
	struct mq_convolve_conf conf;

	/* A MQ client instance is created in start() */
	MqClient *mqc;

	/* Set to false to request thread stop */
	volatile bool can_run;

	/* True if the thread is running */
	volatile bool running;

	char sub_topic[MQ_CONVOLVE_MAX_TOPIC_LEN];
	char pub_topic[MQ_CONVOLVE_MAX_TOPIC_LEN];

	/* Receive scratch buffer and the moving window history itself. Neither data type is known
	 * in advance, both buffers are allocated on the first reception using the received data type. */
	NdArray rxbuf;
	NdArray window;

	/* Number of values received since the last publish. */
	size_t step_count;

	TaskHandle_t task;
} MqConvolve;


mq_convolve_ret_t mq_convolve_init(MqConvolve *self, const struct mq_convolve_conf *conf);
mq_convolve_ret_t mq_convolve_free(MqConvolve *self);
mq_convolve_ret_t mq_convolve_start(MqConvolve *self);
mq_convolve_ret_t mq_convolve_stop(MqConvolve *self);
