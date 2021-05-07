/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ periodogram computation using the Welch method
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
#include <types/ndarray.h>

#include "mq-periodogram.h"

#define MODULE_NAME "mq-periodogram"


static void update_periodogram(MqPeriodogram *self) {
	
}


static void mq_periodogram_task(void *p) {
	MqPeriodogram *self = (MqPeriodogram *)p;

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		/* Receive the message */
		NdArray array = {0};
		struct timespec ts = {0};
		char topic[MQ_PERIODOGRAM_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, MQ_PERIODOGRAM_MAX_TOPIC_LEN, &array, &ts) == MQ_RET_OK) {
			if (array.dtype != self->fifo.dtype) {
				/* Message with an array of the wrong type. */
				continue;
			}
			if (array.asize > self->fifo.asize) {
				/* Message is bigger than the fifo itself. */
				continue;
			}
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("adding %lu values"), array.asize);
			ndarray_move(&self->fifo, 0, array.asize, self->fifo.asize - array.asize);
			ndarray_copy_from(&self->fifo, self->fifo.asize - array.asize, &array, 0, array.asize);
			update_periodogram(self);
		}
	}
	self->running = false;
	vTaskDelete(NULL);
}


mq_periodogram_ret_t mq_periodogram_init(MqPeriodogram *self, Mq *mq) {
	if (u_assert(self != NULL) ||
	    u_assert(mq != NULL)) {
		return MQ_PERIODOGRAM_RET_FAILED;
	}
	memset(self, 0, sizeof(MqPeriodogram));
	self->mq = mq;

	return MQ_PERIODOGRAM_RET_OK;
}


mq_periodogram_ret_t mq_periodogram_free(MqPeriodogram *self) {
	if (u_assert(self != NULL)) {
		return MQ_PERIODOGRAM_RET_FAILED;
	}

	return MQ_PERIODOGRAM_RET_OK;
}


mq_periodogram_ret_t mq_periodogram_start(MqPeriodogram *self, const char *topic_sub, const char *topic_pub, enum dtype dtype, size_t asize) {
	if (u_assert(self != NULL) ||
	    u_assert(topic_sub != NULL) ||
	    u_assert(topic_pub != NULL)) {
		return MQ_PERIODOGRAM_RET_FAILED;
	}

	/* Create a new MQ client instance we will use to publish messages */
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}
	self->mqc->vmt->subscribe(self->mqc, topic_sub);
	strlcpy(self->topic, topic_pub, MQ_PERIODOGRAM_MAX_TOPIC_LEN);

	if (ndarray_init_zero(&self->fifo, dtype, asize) != NDARRAY_RET_OK) {
		goto err;
	}

	xTaskCreate(mq_periodogram_task, "mq-periodogram", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("'%s' -> '%s', periodogram size = %lu"), topic_sub, topic_pub, asize);
	return MQ_PERIODOGRAM_RET_OK;
err:
	/* Not fully started, stop everything. */
	mq_periodogram_stop(self);
	return MQ_PERIODOGRAM_RET_FAILED;
}


mq_periodogram_ret_t mq_periodogram_stop(MqPeriodogram *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->mqc != NULL)) {
		return MQ_PERIODOGRAM_RET_FAILED;
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

	ndarray_free(&self->fifo);

	return MQ_PERIODOGRAM_RET_OK;
}


