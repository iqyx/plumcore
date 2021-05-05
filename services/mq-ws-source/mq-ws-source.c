/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ WaveformSource source
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/waveform_source.h>
#include <interfaces/mq.h>

#include "mq-ws-source.h"

#define MODULE_NAME "mq-ws-source"


static size_t get_buffer_may_receive(MqWsSource *self) {
	/* Find the biggest buffer among all initialized channels .*/
	struct mq_ws_source_channel *ch = self->first_channel;
	size_t max_samples_all = 0;
	while (ch) {
		if (ch->max_samples > max_samples_all) {
			max_samples_all = ch->max_samples;
		}
		ch = ch->next;
	}

	/* And now find the maximum amount of samples we may receive
	 * without overflowing a channel. */
	ch = self->first_channel;
	size_t may_receive = max_samples_all;
	while (ch) {
		/* Count only channels with the buffer allocated. */
		if (ch->max_samples > 0 && ch->buf != NULL) {
			size_t remaining = ch->max_samples - ch->samples;
			if (remaining < may_receive) {
				may_receive = remaining;
			}
		}
		ch = ch->next;
	}
	return may_receive;
}


static void get_source_format(MqWsSource *self) {
	self->source->get_format(self->source->parent, &self->source_format, &self->source_channels);
	switch (self->source_format) {
		case WAVEFORM_SOURCE_FORMAT_U8:
		case WAVEFORM_SOURCE_FORMAT_S8:
			self->source_format_size = 1;
			break;
		case WAVEFORM_SOURCE_FORMAT_U16:
		case WAVEFORM_SOURCE_FORMAT_S16:
			self->source_format_size = 2;
			break;
		case WAVEFORM_SOURCE_FORMAT_U32:
		case WAVEFORM_SOURCE_FORMAT_S32:
		case WAVEFORM_SOURCE_FORMAT_FLOAT:
			self->source_format_size = 4;
			break;
		default:
			self->source_format_size = 0;
			break;
	}
}


static mq_ws_source_ret_t write_channels(MqWsSource *self, size_t samples) {
	struct mq_ws_source_channel *ch = self->first_channel;
	while (ch) {
		u_assert(ch->max_samples > 0);
		if (ch->buf == NULL) {
			/* Buffer space is not allocated yet. */
			ch->buf = malloc(ch->max_samples * self->source_format_size);
			if (ch->buf == NULL) {
				continue;
			}
		}
		size_t remaining = ch->max_samples - ch->samples;
		u_assert(samples <= remaining);

		for (size_t i = 0; i < samples; i++) {
			memcpy(
				/* Copy to the destination channel buffer, offset by number of samples already
				 * buffered and by the current number of samples. */
				(uint8_t *)ch->buf + (ch->samples * self->source_format_size) + (i * self->source_format_size),
				/* Source buffer is offset by the current sample position and by the channel
				 * we are interested in. */
				(uint8_t *)self->rxbuf + (i * self->source_format_size * self->source_channels) + (ch->channel * self->source_format_size),
				self->source_format_size
			);
		}
		ch->samples += samples;

		if (ch->samples == ch->max_samples) {
			struct ndarray array = {
				.array_size = ch->samples
			};
			struct timespec ts = {0};
			self->mqc->vmt->publish(self->mqc, ch->topic, &array, &ts);
			ch->samples = 0;
		}
		
		ch = ch->next;
	}
	return MQ_WS_SOURCE_RET_OK;
}


static void mq_ws_source_task(void *p) {
	MqWsSource *self = (MqWsSource *)p;

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		size_t may_receive = get_buffer_may_receive(self);
		if (may_receive > MQ_WS_SOURCE_RXBUF_SIZE) {
			may_receive = MQ_WS_SOURCE_RXBUF_SIZE;
		}
		size_t read = 0;
		self->source->read(self->source->parent, self->rxbuf, may_receive, &read);
		// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("read %u samples"), read);
		write_channels(self, read);

		/* Tune this value to effectively use the allocated buffer space. */
		vTaskDelay(500);
	}
	self->running = false;
	vTaskDelete(NULL);
}


mq_ws_source_ret_t mq_ws_source_init(MqWsSource *self, WaveformSource *source, Mq *mq) {
	if (u_assert(self != NULL) ||
	    u_assert(source != NULL) ||
	    u_assert(mq != NULL)) {
		return MQ_WS_SOURCE_RET_FAILED;
	}
	memset(self, 0, sizeof(MqWsSource));
	self->source = source;
	self->mq = mq;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return MQ_WS_SOURCE_RET_OK;
}


mq_ws_source_ret_t mq_ws_source_free(MqWsSource *self) {
	if (u_assert(self != NULL)) {
		return MQ_WS_SOURCE_RET_FAILED;
	}

	return MQ_WS_SOURCE_RET_OK;
}


mq_ws_source_ret_t mq_ws_source_start(MqWsSource *self, uint32_t prio) {
	if (u_assert(self != NULL) ||
	    u_assert(self->source != NULL)) {
		return MQ_WS_SOURCE_RET_FAILED;
	}

	get_source_format(self);
	self->rxbuf = malloc(self->source_channels * self->source_format_size * MQ_WS_SOURCE_RXBUF_SIZE);
	if (self->rxbuf == NULL) {
		goto err;
	}

	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}

	xTaskCreate(mq_ws_source_task, "ws-source", configMINIMAL_STACK_SIZE + 256, (void *)self, prio, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	if (self->source->start(self->source->parent) != WAVEFORM_SOURCE_RET_OK) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("started"));
	return MQ_WS_SOURCE_RET_OK;
err:
	/* Not fully started, stop everything. */
	mq_ws_source_stop(self);
	return MQ_WS_SOURCE_RET_FAILED;
}


mq_ws_source_ret_t mq_ws_source_stop(MqWsSource *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->source != NULL)) {
		return MQ_WS_SOURCE_RET_FAILED;
	}
	if (self->source->stop(self->source->parent) != WAVEFORM_SOURCE_RET_OK) {
		goto err;
	}
	/* Stop the thread now. */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(100);
	}
	free(self->rxbuf);
	/** @todo timeout */

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return MQ_WS_SOURCE_RET_OK;
err:
	return MQ_WS_SOURCE_RET_FAILED;
}


mq_ws_source_ret_t mq_ws_source_add_channel(MqWsSource *self, uint8_t channel, const char *topic, size_t max_samples) {
	if (u_assert(self != NULL) ||
	    u_assert(topic != NULL) ||
	    u_assert(max_samples > 0)) {
		return MQ_WS_SOURCE_RET_FAILED;
	}

	struct mq_ws_source_channel *ch = malloc(sizeof(struct mq_ws_source_channel));
	if (ch == NULL) {
		goto err;
	}
	memset(ch, 0, sizeof(struct mq_ws_source_channel));
	ch->channel = channel;
	strlcpy(ch->topic, topic, MQ_WS_SOURCE_MAX_TOPIC_LEN);
	ch->max_samples = max_samples;

	ch->next = self->first_channel;
	self->first_channel = ch;
	
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("added topic '%s' for channel %u, buffer size %u samples"), topic, channel, max_samples);
	return MQ_WS_SOURCE_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("error adding topic '%s' for channel %u"), topic, channel);
	return MQ_WS_SOURCE_RET_FAILED;
}

