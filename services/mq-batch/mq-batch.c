/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ data batching service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/mq.h>

#include "mq-batch.h"

#define MODULE_NAME "mq-batch"


static void mq_batch_task(void *p) {
	MqBatch *self = (MqBatch *)p;

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		NdArray array = {0};
		struct timespec ts = {0};
		char topic[MQ_BATCH_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, MQ_BATCH_MAX_TOPIC_LEN, &array, &ts) == MQ_RET_OK) {
			self->dtype_size = array.dsize;
			self->dtype = array.dtype;
			if (self->buf == NULL) {
				self->buf = malloc(self->dtype_size * self->batch_size);
				if (self->buf == NULL) {
					continue;
				}
			}

			size_t remaining = self->batch_size - self->buf_samples;
			if (array.asize > remaining) {
				continue;
			}

			if (array.buf != NULL && array.bufsize >= (array.asize * array.dsize)) {
				memcpy(
					(uint8_t *)self->buf + self->buf_samples * self->dtype_size,
					array.buf,
					array.asize * array.dsize
				);
			}
			self->buf_samples += array.asize;

			// u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("buf_samples = %u"), self->buf_samples);
			if (self->buf_samples == self->batch_size) {
				/* Reuse the same ndarray */
				ndarray_init_view(&array, self->dtype, self->batch_size, self->buf, self->dtype_size * self->batch_size);
				self->mqc->vmt->publish(self->mqc, self->pub_topic, &array, &ts);
				self->buf_samples = 0;
			}
		}
	}
	self->running = false;
	vTaskDelete(NULL);
}


mq_batch_ret_t mq_batch_init(MqBatch *self, Mq *mq) {
	if (u_assert(self != NULL) ||
	    u_assert(mq != NULL)) {
		return MQ_BATCH_RET_FAILED;
	}
	memset(self, 0, sizeof(MqBatch));
	self->mq = mq;

	return MQ_BATCH_RET_OK;
}


mq_batch_ret_t mq_batch_free(MqBatch *self) {
	if (u_assert(self != NULL)) {
		return MQ_BATCH_RET_FAILED;
	}

	return MQ_BATCH_RET_OK;
}


mq_batch_ret_t mq_batch_start(MqBatch *self, size_t batch_size, const char *sub_topic, const char *pub_topic) {
	if (u_assert(self != NULL) ||
	    u_assert(batch_size > 0) ||
	    u_assert(sub_topic != NULL) ||
	    u_assert(pub_topic != NULL)) {
		return MQ_BATCH_RET_FAILED;
	}

	strlcpy(self->sub_topic, sub_topic, MQ_BATCH_MAX_TOPIC_LEN);
	strlcpy(self->pub_topic, pub_topic, MQ_BATCH_MAX_TOPIC_LEN);
	self->batch_size = batch_size;

	/* Create a new MQ client instance we will use to publish messages */
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}
	self->mqc->vmt->subscribe(self->mqc, sub_topic);

	xTaskCreate(mq_batch_task, "mq-batch", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("'%s' -> '%s', batching %lu values"), sub_topic, pub_topic, batch_size);
	return MQ_BATCH_RET_OK;
err:
	/* Not fully started, stop everything. */
	mq_batch_stop(self);
	return MQ_BATCH_RET_FAILED;
}


mq_batch_ret_t mq_batch_stop(MqBatch *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->mqc != NULL)) {
		return MQ_BATCH_RET_FAILED;
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

	free(self->buf);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return MQ_BATCH_RET_OK;
err:
	return MQ_BATCH_RET_FAILED;
}


