/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MQ moving-window service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Counterpart to the mq-batch service. While mq-batch collects values into disjoint blocks and
 * publishes each block once, mq-convolve keeps a moving history (a sliding window) of the most
 * recently received values and republishes the whole window every @p step newly received values.
 *
 * Consecutive published windows therefore overlap by (window - step) values, which is handy for
 * sliding computations (running statistics, periodograms, convolutions, ...) downstream.
 *
 * The value data type is not configured, it is taken from the first received value. Both buffers
 * are allocated lazily on the first reception once the type is known. Subsequently received values
 * of a different (incompatible) type are reported and ignored.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/mq.h>
#include <types/ndarray.h>

#include "mq-convolve.h"

#define MODULE_NAME "mq-convolve"


/**
 * Append the received values to the moving window, dropping the oldest values to make room. The
 * window keeps at most conf.window values; once it is full each push shifts the contents left.
 */
static void mq_convolve_push(MqConvolve *self, const NdArray *from) {
	size_t cap = self->window.bufsize / self->window.dsize;
	size_t n = from->asize;
	if (n > cap) {
		n = cap;
	}
	if ((self->window.asize + n) > cap) {
		size_t drop = (self->window.asize + n) - cap;
		ndarray_move(&self->window, 0, drop, self->window.asize - drop);
		self->window.asize -= drop;
	}
	ndarray_append(&self->window, from);
}


static void mq_convolve_task(void *p) {
	MqConvolve *self = (MqConvolve *)p;

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		struct timespec ts = {0};
		char topic[MQ_CONVOLVE_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, MQ_CONVOLVE_MAX_TOPIC_LEN, &self->rxbuf, &ts) != MQ_RET_OK) {
			continue;
		}

		/* The receive sets the data type from the message even into an empty buffer. On the very
		 * first reception there are no buffers yet, so allocate them using the received type. This
		 * first value is only used to learn the type and is dropped. */
		if (self->rxbuf.buf == NULL) {
			enum dtype dtype = self->rxbuf.dtype;
			if (ndarray_init_empty(&self->rxbuf, dtype, MQ_CONVOLVE_RXBUF_SAMPLES) != NDARRAY_RET_OK ||
			    ndarray_init_empty(&self->window, dtype, self->conf.window) != NDARRAY_RET_OK) {
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate buffers"));
				break;
			}
			continue;
		}

		/* Once the window data type is fixed, reject values of an incompatible type. */
		if (self->rxbuf.dtype != self->window.dtype) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("ignoring value with incompatible type '%s'"),
			      ndarray_dtype_str(&self->rxbuf));
			continue;
		}

		mq_convolve_push(self, &self->rxbuf);
		self->step_count += self->rxbuf.asize;

		/* Publish the whole window once it is full and enough new values have arrived. */
		if (self->window.asize >= self->conf.window && self->step_count >= self->conf.step) {
			self->mqc->vmt->publish(self->mqc, self->pub_topic, &self->window, &ts);
			self->step_count = 0;
		}
	}
	self->running = false;
	vTaskDelete(NULL);
}


mq_convolve_ret_t mq_convolve_init(MqConvolve *self, const struct mq_convolve_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return MQ_CONVOLVE_RET_NULL;
	}
	memset(self, 0, sizeof(MqConvolve));
	memcpy(&self->conf, conf, sizeof(struct mq_convolve_conf));

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return MQ_CONVOLVE_RET_OK;
}


mq_convolve_ret_t mq_convolve_free(MqConvolve *self) {
	if (u_assert(self != NULL)) {
		return MQ_CONVOLVE_RET_FAILED;
	}

	return MQ_CONVOLVE_RET_OK;
}


mq_convolve_ret_t mq_convolve_start(MqConvolve *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->conf.mq != NULL) ||
	    u_assert(self->conf.window > 0) ||
	    u_assert(self->conf.step > 0) ||
	    u_assert(self->conf.sub_topic != NULL) ||
	    u_assert(self->conf.pub_topic != NULL)) {
		return MQ_CONVOLVE_RET_FAILED;
	}

	strlcpy(self->sub_topic, self->conf.sub_topic, MQ_CONVOLVE_MAX_TOPIC_LEN);
	strlcpy(self->pub_topic, self->conf.pub_topic, MQ_CONVOLVE_MAX_TOPIC_LEN);

	/* Create a new MQ client instance we will use to receive and publish messages */
	self->mqc = self->conf.mq->vmt->open(self->conf.mq);
	if (self->mqc == NULL) {
		goto err;
	}
	self->mqc->vmt->subscribe(self->mqc, self->sub_topic);

	/* The receive and window buffers are allocated on the first reception, once the value data
	 * type carried by the message is known. */

	xTaskCreate(mq_convolve_task, "mq-convolve", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("'%s' -> '%s', window %lu values, step %lu values"),
	      self->sub_topic, self->pub_topic, self->conf.window, self->conf.step);
	return MQ_CONVOLVE_RET_OK;
err:
	/* Not fully started, stop everything. */
	mq_convolve_stop(self);
	return MQ_CONVOLVE_RET_FAILED;
}


mq_convolve_ret_t mq_convolve_stop(MqConvolve *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->mqc != NULL)) {
		return MQ_CONVOLVE_RET_FAILED;
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

	/* Both buffers may be unallocated if no value was ever received; ndarray_free handles that. */
	ndarray_free(&self->rxbuf);
	ndarray_free(&self->window);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return MQ_CONVOLVE_RET_OK;
}
