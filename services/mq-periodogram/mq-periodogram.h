/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ periodogram computation using the Welch method
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

#define MQ_PERIODOGRAM_MAX_TOPIC_LEN 32

typedef enum {
	MQ_PERIODOGRAM_RET_OK = 0,
	MQ_PERIODOGRAM_RET_FAILED,
} mq_periodogram_ret_t;

typedef struct {
	Mq *mq;

	/* A MQ client instance is created in init() */
	MqClient *mqc;

	/* Set to false to request thread stop */
	volatile bool can_run;

	/* True if the thread is running */
	volatile bool running;

	char topic[MQ_PERIODOGRAM_MAX_TOPIC_LEN];
	uint32_t avg_count;

	TaskHandle_t task;

	NdArray rxbuf;
	NdArray fifo;
	NdArray tmp1;
	NdArray tmp2;
	NdArray periodogram;
	uint32_t periodogram_count;
	
} MqPeriodogram;


mq_periodogram_ret_t mq_periodogram_init(MqPeriodogram *self, Mq *mq);
mq_periodogram_ret_t mq_periodogram_free(MqPeriodogram *self);
mq_periodogram_ret_t mq_periodogram_start(MqPeriodogram *self, const char *topic_sub, const char *topic_pub, enum dtype dtype, size_t asize);
mq_periodogram_ret_t mq_periodogram_stop(MqPeriodogram *self);
