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
	/* Find the biggest buffer among all initialized channels. */
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


/* Count bytes used by a single sample of the WaveformSource data stream */
static void get_source_format(MqWsSource *self) {
	self->source->get_format(self->source->parent, &self->source_format, &self->source_channels);
	switch (self->source_format) {
		case WAVEFORM_SOURCE_FORMAT_U8:
			self->source_dtype = DTYPE_UINT8;
			break;
		case WAVEFORM_SOURCE_FORMAT_S8:
			self->source_dtype = DTYPE_INT8;
			break;
		case WAVEFORM_SOURCE_FORMAT_U16:
			self->source_dtype = DTYPE_UINT16;
			break;
		case WAVEFORM_SOURCE_FORMAT_S16:
			self->source_dtype = DTYPE_INT16;
			break;
		case WAVEFORM_SOURCE_FORMAT_U32:
			self->source_dtype = DTYPE_UINT32;
			break;
		case WAVEFORM_SOURCE_FORMAT_S32:
			self->source_dtype = DTYPE_INT32;
			break;
		case WAVEFORM_SOURCE_FORMAT_FLOAT:
			self->source_dtype = DTYPE_FLOAT;
			break;
		default:
			self->source_format_size = 0;
			break;
	}
	self->source_format_size = ndarray_get_dsize(self->source_dtype);
}


static mq_ws_source_ret_t write_channels(MqWsSource *self, size_t samples) {
	struct mq_ws_source_channel *ch = self->first_channel;
	while (ch) {
		/* Zero max_samples mean a configuration error. We cannot use such channel. */
		if (u_assert(ch->max_samples > 0)) {
			continue;
		}
		/* Now the channel buffer size (in samples) is known, check if the
		 * buffer is allocated. */
		if (ch->buf == NULL) {
			ch->buf = malloc(ch->max_samples * self->source_format_size);
			if (ch->buf == NULL) {
				/* Cannot allocate any buffer space, continue as we
				 * cannot use channel without a buffer. */
				continue;
			}
		}
		/* Compute the number of remaining samples which can be appended
		 * to the current channel buffer. If we are requested to add more
		 * samples, something went wrong. */
		size_t remaining = ch->max_samples - ch->samples;
		u_assert(samples <= remaining);

		/* Perform some data twiddling to get them in the right format. */
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

		/* Check if the buffer is full. There may be zero, one or more
		 * channels with full buffers. Publish their buffers to the MQ. */
		if (ch->samples == ch->max_samples) {
			/* Initialize the metadata first. */
			NdArray array;
			ndarray_init_view(&array, DTYPE_INT16, ch->samples, ch->buf, ch->samples * self->source_format_size);

			/** @todo get the exact sample time from somewhere */
			struct timespec ts = {0};
			if (self->ts_clock && self->ts_clock->get != NULL) {
				self->ts_clock->get(self->ts_clock->parent, &ts);
			}

			/* Publish the array and clear the channel buffer. */
			self->mqc->vmt->publish(self->mqc, ch->topic, &array, &ts);
			ch->samples = 0;
		}

		ch = ch->next;
	}
	return MQ_WS_SOURCE_RET_OK;
}


/* A single thread is used to receive WaveformSource data stream and split
 * it into multiple channels. When channel buffers are full, publish the data
 * to the MQ. */
static void mq_ws_source_task(void *p) {
	MqWsSource *self = (MqWsSource *)p;

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		/* Compute the number of samples we may safely receive without
		 * overflowing channel buffers. The amount of free space in all
		 * active channels may vary. */
		size_t may_receive = get_buffer_may_receive(self);
		if (may_receive > MQ_WS_SOURCE_RXBUF_SIZE) {
			may_receive = MQ_WS_SOURCE_RXBUF_SIZE;
		}

		/* Read maximum of may_receive samples, but keep in mind that the
		 * actual number of received samples may be lower. */
		size_t read = 0;
		self->source->read(self->source->parent, self->rxbuf, may_receive, &read);

		/* Traverse all channels and copy samples into channel buffers. */
		write_channels(self, read);

		vTaskDelay(self->read_period_ms);
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
	self->read_period_ms = MQ_WS_SOURCE_READ_PERIOD_MS;

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

	/* The WaveformSource dependency is valid. Query it to determine the sample format. */
	get_source_format(self);
	self->rxbuf = malloc(self->source_channels * self->source_format_size * MQ_WS_SOURCE_RXBUF_SIZE);
	if (self->rxbuf == NULL) {
		goto err;
	}

	/* Create a new MQ client instance we will use to publish messages */
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}

	/* And when everything is ready, start the task */
	xTaskCreate(mq_ws_source_task, "ws-source", configMINIMAL_STACK_SIZE + 256, (void *)self, prio, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	/* And enable the source to get some data in. */
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
	/** @todo timeout */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(100);
	}

	if (self->mqc) {
		self->mqc->vmt->close(self->mqc);
	}

	free(self->rxbuf);

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

	/* Allocate a new structure holding data about a single channel. */
	struct mq_ws_source_channel *ch = malloc(sizeof(struct mq_ws_source_channel));
	if (ch == NULL) {
		goto err;
	}
	memset(ch, 0, sizeof(struct mq_ws_source_channel));
	ch->channel = channel;
	strlcpy(ch->topic, topic, MQ_WS_SOURCE_MAX_TOPIC_LEN);
	ch->max_samples = max_samples;

	/* And append it to the linked list */
	ch->next = self->first_channel;
	self->first_channel = ch;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("added topic '%s' for channel %u, buffer size %u samples"), topic, channel, max_samples);
	return MQ_WS_SOURCE_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("error adding topic '%s' for channel %u"), topic, channel);
	return MQ_WS_SOURCE_RET_FAILED;
}


mq_ws_source_ret_t mq_ws_source_set_ts_clock(MqWsSource *self, Clock *ts_clock) {
	if (u_assert(self != NULL) ||
	    u_assert(ts_clock != NULL)) {
		return MQ_WS_SOURCE_RET_FAILED;
	}

	self->ts_clock = ts_clock;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("setting timestamping clock 0x%08x"), ts_clock);

	return MQ_WS_SOURCE_RET_OK;
}

