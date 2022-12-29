/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ data statistics service
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
#include <arm_math.h>

#include "mq-stats.h"

#define MODULE_NAME "mq-stats"


static float rms(int16_t data[], size_t len) {
	int32_t res = 0;
	for (size_t i = 0; i < len; i++) {
		res += data[i] * data[i];
	}
	return sqrtf(res / len);
}


static float mean(int16_t data[], size_t len) {
	float sum = 0;
	for (size_t i = 0; i < len; i++) {
		sum += (float)data[i];
	}
	return sum / (float)len;
}

static float var(int16_t data[], size_t len) {
	float m = mean(data, len);
	float sumsq = 0;
	for (size_t i = 0; i < len; i++) {
		sumsq += (m - (float)data[i]) * (m - (float)data[i]);
	}
	return sumsq / (float)len;
}


static float nrms(int16_t data[], size_t len) {
	return sqrtf(var(data, len));
}


static float psd(int16_t data[], size_t len, float bandwidth) {
	return nrms(data, len) / sqrtf(bandwidth);
}


static float snr_db(int16_t data[], size_t len, float full_scale) {
	return 20.0 * log10f((full_scale / ((full_scale / 2.0) * sqrtf(full_scale / 2.0))) / nrms(data, len));
}


static float enob(int16_t data[], size_t len, float full_scale) {
	return (snr_db(data, len, full_scale) - 1.76) / 6.02;
}


static void publish_float(MqClient *mqc, const char *topic, float value) {
	NdArray array;
	ndarray_init_view(&array, DTYPE_FLOAT, 1, &value, sizeof(float));
	struct timespec ts = {0};
	mqc->vmt->publish(mqc, topic, &array, &ts);
}


static void publish_int32(MqClient *mqc, const char *topic, int32_t value) {
	NdArray array;
	ndarray_init_view(&array, DTYPE_INT32, 1, &value, sizeof(int32_t));
	struct timespec ts = {0};
	mqc->vmt->publish(mqc, topic, &array, &ts);
}


static void mq_stats_task(void *p) {
	MqStats *self = (MqStats *)p;

	self->can_run = true;
	self->running = true;
	while (self->can_run) {
		/* Receive the message */
		struct timespec ts = {0};
		char topic[MQ_STATS_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, MQ_STATS_MAX_TOPIC_LEN, &self->buf, &ts) == MQ_RET_OK) {
			char new_topic[MQ_STATS_MAX_TOPIC_LEN] = {0};
			/* Compute the requested statistics. Manually.
			 * Use ndarray functions instead once they become available. */
			if (self->buf.dtype == DTYPE_INT16) {
				if (self->e & MQ_STATS_RMS) {
					float f = rms((int16_t *)self->buf.buf, self->buf.asize);
					strlcpy(new_topic, topic, MQ_STATS_MAX_TOPIC_LEN);
					strlcat(new_topic, "/rms", MQ_STATS_MAX_TOPIC_LEN);
					publish_float(self->mqc, new_topic, f);
				}
				if (self->e & MQ_STATS_MEAN) {
					float f = mean((int16_t *)self->buf.buf, self->buf.asize);
					strlcpy(new_topic, topic, MQ_STATS_MAX_TOPIC_LEN);
					strlcat(new_topic, "/mean", MQ_STATS_MAX_TOPIC_LEN);
					publish_float(self->mqc, new_topic, f);
				}
			}
			if (self->buf.dtype == DTYPE_INT32) {
				if (self->e & MQ_STATS_MEAN) {
					int32_t res = 0;
					arm_mean_q31((const q31_t *)self->buf.buf, self->buf.asize, (q31_t *)res);
					strlcpy(new_topic, topic, MQ_STATS_MAX_TOPIC_LEN);
					strlcat(new_topic, "/mean", MQ_STATS_MAX_TOPIC_LEN);
					publish_int32(self->mqc, new_topic, *(int32_t *)&res);
				}
			}

		}
	}
	self->running = false;
	vTaskDelete(NULL);
}


mq_stats_ret_t mq_stats_init(MqStats *self, Mq *mq) {
	if (u_assert(self != NULL) ||
	    u_assert(mq != NULL)) {
		return MQ_STATS_RET_FAILED;
	}
	memset(self, 0, sizeof(MqStats));
	self->mq = mq;

	return MQ_STATS_RET_OK;
}


mq_stats_ret_t mq_stats_free(MqStats *self) {
	if (u_assert(self != NULL)) {
		return MQ_STATS_RET_FAILED;
	}

	return MQ_STATS_RET_OK;
}


mq_stats_ret_t mq_stats_start(MqStats *self, const char *topic, enum dtype dtype, size_t asize) {
	if (u_assert(self != NULL) ||
	    u_assert(topic != NULL)) {
		return MQ_STATS_RET_FAILED;
	}

	strlcpy(self->topic, topic, MQ_STATS_MAX_TOPIC_LEN);

	/* Create a new MQ client instance we will use to publish messages */
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}
	self->mqc->vmt->subscribe(self->mqc, topic);

	if (ndarray_init_empty(&self->buf, dtype, asize) != NDARRAY_RET_OK) {
		goto err;
	}

	xTaskCreate(mq_stats_task, "mq-stats", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	return MQ_STATS_RET_OK;
err:
	/* Not fully started, stop everything. */
	mq_stats_stop(self);
	return MQ_STATS_RET_FAILED;
}


mq_stats_ret_t mq_stats_stop(MqStats *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->mqc != NULL)) {
		return MQ_STATS_RET_FAILED;
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

	ndarray_free(&self->buf);

	return MQ_STATS_RET_OK;
// err:
	// return MQ_STATS_RET_FAILED;
}


mq_stats_ret_t mq_stats_enable(MqStats *self, enum mq_stats_enabled e) {
	if (u_assert(self != NULL)) {
		return MQ_STATS_RET_FAILED;
	}
	self->e = e;
	char s[100] = {0};
	if (self->e & MQ_STATS_RMS) strlcat(s, "rms,", sizeof(s));
	if (self->e & MQ_STATS_MEAN) strlcat(s, "mean,", sizeof(s));
	if (self->e & MQ_STATS_VAR) strlcat(s, "var,", sizeof(s));
	if (self->e & MQ_STATS_NRMS) strlcat(s, "nrms,", sizeof(s));
	if (self->e & MQ_STATS_PSD) strlcat(s, "psd,", sizeof(s));
	if (self->e & MQ_STATS_SNR) strlcat(s, "snr,", sizeof(s));
	if (self->e & MQ_STATS_ENOB) strlcat(s, "enob,", sizeof(s));
	if (strlen(s) > 0) {
		s[strlen(s) - 1] = '\0';
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("compute [%s] statistics for '%s'"), s, self->topic);

	return MQ_STATS_RET_OK;
}
