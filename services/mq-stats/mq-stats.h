/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ data statistics service
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
#include <types/ndarray.h>

#define MQ_STATS_MAX_TOPIC_LEN 32

typedef enum {
	MQ_STATS_RET_OK = 0,
	MQ_STATS_RET_FAILED,
} mq_stats_ret_t;

enum mq_stats_enabled {
	MQ_STATS_RMS =        (1 << 0),
	MQ_STATS_MEAN =       (1 << 1),
	MQ_STATS_VAR =        (1 << 2),
	MQ_STATS_NRMS =       (1 << 3),
	MQ_STATS_PSD =        (1 << 4),
	MQ_STATS_SNR =        (1 << 5),
	MQ_STATS_ENOB =       (1 << 6),
};

typedef struct {
	Mq *mq;

	/* A MQ client instance is created in init() */
	MqClient *mqc;

	/* Set to false to request thread stop */
	volatile bool can_run;

	/* True if the thread is running */
	volatile bool running;

	char topic[MQ_STATS_MAX_TOPIC_LEN];
	enum mq_stats_enabled e;
	NdArray buf;

	TaskHandle_t task;
	
} MqStats;


mq_stats_ret_t mq_stats_init(MqStats *self, Mq *mq);
mq_stats_ret_t mq_stats_free(MqStats *self);
mq_stats_ret_t mq_stats_start(MqStats *self, const char *topic, enum dtype dtype, size_t asize);
mq_stats_ret_t mq_stats_stop(MqStats *self);
mq_stats_ret_t mq_stats_enable(MqStats *self, enum mq_stats_enabled e);
