/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ data batching service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"

#include <interfaces/mq.h>

#define MQ_BATCH_MAX_TOPIC_LEN 32

typedef enum {
	MQ_BATCH_RET_OK = 0,
	MQ_BATCH_RET_FAILED,
} mq_batch_ret_t;

typedef struct {
	Mq *mq;

	/* A MQ client instance is created in init() */
	MqClient *mqc;

	/* Set to false to request thread stop */
	volatile bool can_run;

	/* True if the thread is running */
	volatile bool running;

	char sub_topic[MQ_BATCH_MAX_TOPIC_LEN];
	char pub_topic[MQ_BATCH_MAX_TOPIC_LEN];
	size_t batch_size;
	size_t dtype_size;
	enum dtype dtype;
	void *buf;
	size_t buf_samples;

	TaskHandle_t task;
} MqBatch;


mq_batch_ret_t mq_batch_init(MqBatch *self, Mq *mq);
mq_batch_ret_t mq_batch_free(MqBatch *self);
mq_batch_ret_t mq_batch_start(MqBatch *self, size_t batch_size, const char *sub_topic, const char *pub_topic);
mq_batch_ret_t mq_batch_stop(MqBatch *self);
