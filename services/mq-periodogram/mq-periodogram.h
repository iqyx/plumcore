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
#define MQ_PERIODOGRAM_MAX_INPUT_BUF_LEN 512

typedef enum {
	MQ_PERIODOGRAM_RET_OK = 0,
	MQ_PERIODOGRAM_RET_FAILED,
} mq_periodogram_ret_t;

enum mq_periodogram_window {
	MQ_PERIODOGRAM_WINDOW_NONE = 0,
	MQ_PERIODOGRAM_WINDOW_HAMMING,
};

typedef struct {
	Mq *mq;
	MqClient *mqc;

	volatile bool can_run;
	volatile bool running;
	TaskHandle_t task;

	char topic[MQ_PERIODOGRAM_MAX_TOPIC_LEN];

	NdArray rxbuf;
	NdArray fifo;
	NdArray tmp1;
	NdArray tmp2;
	NdArray periodogram;

	uint32_t periodogram_count;
	uint32_t period;
	enum mq_periodogram_window window;
} MqPeriodogram;


mq_periodogram_ret_t mq_periodogram_init(MqPeriodogram *self, Mq *mq);
mq_periodogram_ret_t mq_periodogram_free(MqPeriodogram *self);
mq_periodogram_ret_t mq_periodogram_start(MqPeriodogram *self, const char *topic_sub, const char *topic_pub, enum dtype dtype, size_t asize);
mq_periodogram_ret_t mq_periodogram_stop(MqPeriodogram *self);
mq_periodogram_ret_t mq_periodogram_set_period(MqPeriodogram *self, uint32_t period);
mq_periodogram_ret_t mq_periodogram_set_window(MqPeriodogram *self, enum mq_periodogram_window window);

