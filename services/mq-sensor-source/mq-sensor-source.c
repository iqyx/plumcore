/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * MQ sensor source, periodically publishing sensor values
 *
 * Copyright (c) 2023-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <main.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/sensor.h>
#include <interfaces/mq.h>
#include <interfaces/clock.h>
#include <types/ndarray.h>

#include "mq-sensor-source.h"

#define MODULE_NAME "mq-sensor-source"


static void mq_sensor_source_task(void *p) {
	MqSensorSource *self = (MqSensorSource *)p;

	self->can_run = true;
	self->running = true;
	TickType_t last = xTaskGetTickCount();
	while (self->can_run) {
		vTaskDelayUntil(&last, pdMS_TO_TICKS(self->conf.period_ms));

		/* Get the time first, all values of this period share it. Skip the period if the clock fails. */
		struct timespec ts = {0};
		if (self->conf.clock != NULL && self->conf.clock->vmt->get(self->conf.clock, &ts) != CLOCK_RET_OK) {
			continue;
		}

		for (const struct mq_sensor_source_sensor *s = self->conf.sensors; s->topic != NULL; s++) {
			float f = 0.0f;
			if (s->sensor == NULL || s->sensor->vmt->value_f == NULL || s->sensor->vmt->value_f(s->sensor, &f) != SENSOR_RET_OK) {
				continue;
			}

			NdArray d;
			ndarray_init_view(&d, DTYPE_FLOAT, 1, &f, sizeof(f));
			self->mqc->vmt->publish(self->mqc, s->topic, &d, &ts);
		}
	}
	self->running = false;
	vTaskDelete(NULL);
}


mq_sensor_source_ret_t mq_sensor_source_init(MqSensorSource *self, const struct mq_sensor_source_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return MQ_SENSOR_SOURCE_RET_NULL;
	}
	memset(self, 0, sizeof(MqSensorSource));
	memcpy(&self->conf, conf, sizeof(struct mq_sensor_source_conf));

	return MQ_SENSOR_SOURCE_RET_OK;
}


mq_sensor_source_ret_t mq_sensor_source_free(MqSensorSource *self) {
	if (u_assert(self != NULL)) {
		return MQ_SENSOR_SOURCE_RET_NULL;
	}

	return MQ_SENSOR_SOURCE_RET_OK;
}


mq_sensor_source_ret_t mq_sensor_source_start(MqSensorSource *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->conf.mq != NULL) ||
	    u_assert(self->conf.sensors != NULL) ||
	    u_assert(self->conf.period_ms > 0)) {
		return MQ_SENSOR_SOURCE_RET_FAILED;
	}

	/* Create a new MQ client instance we will use to publish the values */
	self->mqc = self->conf.mq->vmt->open(self->conf.mq);
	if (self->mqc == NULL) {
		goto err;
	}

	xTaskCreate(mq_sensor_source_task, "mq-sensor-source", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	for (const struct mq_sensor_source_sensor *s = self->conf.sensors; s->topic != NULL; s++) {
		if (s->sensor != NULL) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("publishing to '%s' every %lu ms"), s->topic, self->conf.period_ms);
		}
	}
	return MQ_SENSOR_SOURCE_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the service"));
	/* Not fully started, stop everything. */
	mq_sensor_source_stop(self);
	return MQ_SENSOR_SOURCE_RET_FAILED;
}


mq_sensor_source_ret_t mq_sensor_source_stop(MqSensorSource *self) {
	if (u_assert(self != NULL)) {
		return MQ_SENSOR_SOURCE_RET_NULL;
	}

	/* Stop the thread now. */
	/** @todo timeout */
	self->can_run = false;
	while (self->running) {
		vTaskDelay(100);
	}

	if (self->mqc != NULL) {
		self->mqc->vmt->close(self->mqc);
		self->mqc = NULL;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return MQ_SENSOR_SOURCE_RET_OK;
}
