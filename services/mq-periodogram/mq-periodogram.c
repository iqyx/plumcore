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

/* Ignore undefined __ARM_FEATURE_MVE warning in the CMSIS-DSP library. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
	#include "dsp/support_functions.h"
	#include "dsp/filtering_functions.h"
	#include "dsp/transform_functions.h"
#pragma GCC diagnostic pop

#include <interfaces/mq.h>
#include <types/ndarray.h>

#include "mq-periodogram.h"

#define MODULE_NAME "mq-periodogram"


/* There is a new data in the FIFO. Compute FFT of the FIFO
 * and update the periodogram. Publish the resulting periodogram
 * if enough passes were done. */
static void update_periodogram(MqPeriodogram *self) {
	if (self->fifo.dtype != DTYPE_INT16) {
		return;
	}

	arm_rfft_fast_instance_f32 s;
	arm_rfft_fast_init_f32(&s, self->fifo.asize);

	/* Prepare the input buffer. Convert to float. */
	self->tmp1.asize = self->fifo.asize;
	for (size_t i = 0; i < self->fifo.asize; i++) {
		((float *)self->tmp1.buf)[i] = ((int16_t *)self->fifo.buf)[i];
	}

	/* Compute FFT and convert the output. */
	self->tmp2.asize = self->fifo.asize;
	arm_rfft_fast_f32(&s, (float *)self->tmp1.buf, (float *)self->tmp2.buf, 0);
	self->tmp1.asize = self->fifo.asize / 2;
	arm_cmplx_mag_f32((float *)self->tmp2.buf, (float *)self->tmp1.buf, self->tmp1.asize);

	/* Append the squared spectrum to the periodogram. */
	float *from = (float *)self->tmp1.buf;
	float *to = (float *)self->periodogram.buf;
	for (size_t i = 0; i < self->periodogram.asize; i++) {
		to[i] += from[i] * from[i];
	}
	self->periodogram_count++;
}


static void mq_periodogram_task(void *p) {
	MqPeriodogram *self = (MqPeriodogram *)p;

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		/* Receive the message */
		struct timespec ts = {0};
		char topic[MQ_PERIODOGRAM_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, MQ_PERIODOGRAM_MAX_TOPIC_LEN, &self->rxbuf, &ts) == MQ_RET_OK) {
			if (self->rxbuf.dtype != self->fifo.dtype) {
				/* Message with an array of the wrong type. */
				continue;
			}
			if (self->rxbuf.asize > self->fifo.asize) {
				/* Message is bigger than the fifo itself. */
				continue;
			}
			ndarray_move(&self->fifo, 0, self->rxbuf.asize, self->fifo.asize - self->rxbuf.asize);
			ndarray_copy_from(&self->fifo, self->fifo.asize - self->rxbuf.asize, &self->rxbuf, 0, self->rxbuf.asize);
			update_periodogram(self);
			if (self->periodogram_count >= 2) {
				ndarray_sqrt(&self->periodogram);
				self->mqc->vmt->publish(self->mqc, self->topic, &self->periodogram, &ts);
				ndarray_zero(&self->periodogram);
			}
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

	if (ndarray_init_zero(&self->rxbuf, dtype, 512) != NDARRAY_RET_OK) {
		goto err;
	}
	if (ndarray_init_zero(&self->fifo, dtype, asize) != NDARRAY_RET_OK) {
		goto err;
	}
	if (ndarray_init_zero(&self->tmp1, DTYPE_FLOAT, asize) != NDARRAY_RET_OK) {
		goto err;
	}
	if (ndarray_init_zero(&self->tmp2, DTYPE_FLOAT, asize) != NDARRAY_RET_OK) {
		goto err;
	}
	if (ndarray_init_zero(&self->periodogram, DTYPE_FLOAT, asize / 2) != NDARRAY_RET_OK) {
		goto err;
	}
	self->periodogram_count = 0;

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

	ndarray_free(&self->rxbuf);
	ndarray_free(&self->fifo);
	ndarray_free(&self->tmp1);
	ndarray_free(&self->tmp2);
	ndarray_free(&self->periodogram);

	return MQ_PERIODOGRAM_RET_OK;
}


