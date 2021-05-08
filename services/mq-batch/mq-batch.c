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
		struct timespec ts = {0};
		char topic[MQ_BATCH_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, MQ_BATCH_MAX_TOPIC_LEN, &self->rxbuf, &ts) == MQ_RET_OK) {
			ndarray_append(&self->batch, &self->rxbuf);
			// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("received %lu, batch size %lu"), self->rxbuf.asize, self->batch.asize);
			if ((self->batch.asize * self->batch.dsize) >= self->batch.bufsize) {
				self->mqc->vmt->publish(self->mqc, self->pub_topic, &self->batch, &ts);
				self->batch.asize = 0;
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


mq_batch_ret_t mq_batch_start(MqBatch *self, enum dtype dtype, size_t asize, const char *sub_topic, const char *pub_topic) {
	if (u_assert(self != NULL) ||
	    u_assert(asize > 0) ||
	    u_assert(sub_topic != NULL) ||
	    u_assert(pub_topic != NULL)) {
		return MQ_BATCH_RET_FAILED;
	}

	strlcpy(self->sub_topic, sub_topic, MQ_BATCH_MAX_TOPIC_LEN);
	strlcpy(self->pub_topic, pub_topic, MQ_BATCH_MAX_TOPIC_LEN);

	/* Create a new MQ client instance we will use to publish messages */
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}
	self->mqc->vmt->subscribe(self->mqc, sub_topic);

	if (ndarray_init_empty(&self->batch, dtype, asize) != NDARRAY_RET_OK) {
		goto err;
	}
	if (ndarray_init_empty(&self->rxbuf, dtype, 32) != NDARRAY_RET_OK) {
		goto err;
	}

	xTaskCreate(mq_batch_task, "mq-batch", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("'%s' -> '%s', batching %lu values"), sub_topic, pub_topic, self->batch.bufsize / self->batch.dsize);
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

	ndarray_free(&self->rxbuf);
	ndarray_free(&self->batch);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return MQ_BATCH_RET_OK;
err:
	return MQ_BATCH_RET_FAILED;
}


