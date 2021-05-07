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


#define MQ_WS_SOURCE_MAX_TOPIC_LEN CONFIG_SERVICE_MQ_WS_SOURCE_MAX_TOPIC_LEN
#define MQ_WS_SOURCE_RXBUF_SIZE CONFIG_SERVICE_MQ_WS_SOURCE_RXBUF_SIZE
#define MQ_WS_SOURCE_READ_PERIOD_MS CONFIG_SERVICE_MQ_WS_SOURCE_READ_PERIOD_MS

typedef enum {
	MQ_WS_SOURCE_RET_OK = 0,
	MQ_WS_SOURCE_RET_FAILED,
} mq_ws_source_ret_t;

struct mq_ws_source_channel {
	/* Number of the channel in the WaveformSource data stream */
	uint8_t channel;

	/* Topic of the channel */
	char topic[MQ_WS_SOURCE_MAX_TOPIC_LEN];

	/* Channel buffer. It is published to the message queue as a whole.
	 * @p max_samples is the size of the buffer, whereas @p samples is
	 * the actual number of samples residing in the buffer. */
	size_t max_samples;
	size_t samples;
	void *buf;

	/* Channels are arranged as a linked list. */
	struct mq_ws_source_channel *next;
};

typedef struct {
	/* Dependencies injected in init() */
	WaveformSource *source;
	Mq *mq;

	/* A MQ client instance is created in init() */
	MqClient *mqc;

	/* Set to false to request thread stop */
	volatile bool can_run;

	/* True if the thread is running */
	volatile bool running;

	TaskHandle_t task;

	/* We need to know the format of the source data stream. It defines the sample size.
	 * When the source is available, note both. */
	enum waveform_source_format source_format;
	size_t source_format_size;
	enum dtype source_dtype;

	/* Number of channels in the source data stream. */
	uint32_t source_channels;

	/* Buffer used to receive data from the source stream. It has @p MQ_WS_SOURCE_RXBUF_SIZE size. */
	void *rxbuf;
	uint32_t read_period_ms;

	/* Start of the linked list */
	struct mq_ws_source_channel *first_channel;
} MqWsSource;


/**
 * @brief Initialize the MqWsSource service
 *
 * @param self The instance to initialize. Must be allocated beforehand.
 * @param source A valid WaveformSource interface dependency
 * @param mq A valid MessageQueue interface dependency
 *
 * @return MQ_WS_SOURCE_RET_FAILED on error or MQ_WS_SOURCE_RET_OK otherwise.
 */
mq_ws_source_ret_t mq_ws_source_init(MqWsSource *self, WaveformSource *source, Mq *mq);


mq_ws_source_ret_t mq_ws_source_free(MqWsSource *self);


mq_ws_source_ret_t mq_ws_source_start(MqWsSource *self, uint32_t prio);


mq_ws_source_ret_t mq_ws_source_stop(MqWsSource *self);


mq_ws_source_ret_t mq_ws_source_add_channel(MqWsSource *self, uint8_t channel, const char *topic, size_t max_samples);
