/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ WaveformSource source
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

#include <interfaces/waveform_source.h>
#include <interfaces/mq.h>


#define MQ_WS_SOURCE_MAX_TOPIC_LEN 32
#define MQ_WS_SOURCE_RXBUF_SIZE 32

typedef enum {
	MQ_WS_SOURCE_RET_OK = 0,
	MQ_WS_SOURCE_RET_FAILED,
} mq_ws_source_ret_t;

struct mq_ws_source_channel {
	uint8_t channel;
	char topic[MQ_WS_SOURCE_MAX_TOPIC_LEN];

	size_t max_samples;
	size_t samples;
	void *buf;

	struct mq_ws_source_channel *next;
};

typedef struct {
	WaveformSource *source;
	Mq *mq;
	MqClient *mqc;

	volatile bool can_run;
	volatile bool running;
	TaskHandle_t task;

	enum waveform_source_format source_format;
	size_t source_format_size;
	uint32_t source_channels;
	struct mq_ws_source_channel *first_channel;
	void *rxbuf;
} MqWsSource;


mq_ws_source_ret_t mq_ws_source_init(MqWsSource *self, WaveformSource *source, Mq *mq);
mq_ws_source_ret_t mq_ws_source_free(MqWsSource *self);
mq_ws_source_ret_t mq_ws_source_start(MqWsSource *self, uint32_t prio);
mq_ws_source_ret_t mq_ws_source_stop(MqWsSource *self);
mq_ws_source_ret_t mq_ws_source_add_channel(MqWsSource *self, uint8_t channel, const char *topic, size_t max_samples);
