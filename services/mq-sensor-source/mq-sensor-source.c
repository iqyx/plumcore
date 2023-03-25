/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ sensor source
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
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

#include <interfaces/sensor.h>
#include <interfaces/mq.h>
#include <types/ndarray.h>

#include "mq-sensor-source.h"

#define MODULE_NAME "mq-sensor-source"



static void mq_sensor_source_task(void *p) {
	MqSensorSource *self = (MqSensorSource *)p;

	while (true) {
		vTaskDelay(self->period_ms);

		/* Get the time first */
		struct timespec ts = {0};
		/* Ignore the clock if not set. */
		if (self->clock && self->clock->get != NULL) {
			/* But fail if set and unable to get it. */
			if (self->clock->get(self->clock->parent, &ts) != CLOCK_RET_OK) {
				continue;
			}
		}

		/* Capture the sensor value */
		float f = 0.0f;
		if (self->sensor != NULL && self->sensor->vmt->value_f != NULL) {
			if (self->sensor->vmt->value_f(self->sensor, &f) != SENSOR_RET_OK) {
				continue;
			}
		}

		NdArray d;
		ndarray_init_view(&d, DTYPE_FLOAT, 1, &f, sizeof(f));
		self->mqc->vmt->publish(self->mqc, self->topic, &d, &ts);
	}
	vTaskDelete(NULL);
}


mq_sensor_source_ret_t mq_sensor_source_init(MqSensorSource *self, Sensor *sensor, const char *topic, Mq *mq, Clock *clock, uint32_t period_ms) {
	memset(self, 0, sizeof(MqSensorSource));

	self->sensor = sensor;
	self->topic = topic;
	self->mq = mq;
	self->clock = clock;
	self->period_ms = period_ms;

	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}

	xTaskCreate(mq_sensor_source_task, "sensor-source", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("publishing sensor value to '%s' every %u ms"), topic, period_ms);
	return MQ_SENSOR_SOURCE_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the service"));
	return MQ_SENSOR_SOURCE_RET_FAILED;
}


mq_sensor_source_ret_t mq_sensor_source_free(MqSensorSource *self) {
	(void)self;
	return MQ_SENSOR_SOURCE_RET_OK;
}


