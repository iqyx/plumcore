/*
 * Copyright (c) 2017, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "stdint.h"
#include "stdbool.h"
#include "stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "interfaces/sensor.h"
#include "data-process.h"
#include "sensor-source.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "dp-sensor-source"


static void dp_sensor_source_task(void *p) {
	struct dp_sensor_source *self = (struct dp_sensor_source *)p;

	self->running = true;
	while (self->can_run) {
		if (dp_output_is_connected(&self->out)) {
			float value;
			interface_sensor_value(self->sensor, &value);

			value *= self->multiplier;
			value += self->offset;

			struct dp_data data = {
				.len = 1,
				.flags = DP_DATA_FRAME_START | DP_DATA_FRAME_END,
				.type = DP_DATA_TYPE_FLOAT,
				.content.dp_float = &value,
			};
			dp_output_write(&self->out, &data);
		}
		vTaskDelay(self->interval_ms);
	}
	self->running = false;

	vTaskDelete(NULL);
}


dp_ret_t dp_sensor_source_init(struct dp_sensor_source *self, const char *name) {
	if (u_assert(self != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	memset(self, 0, sizeof(struct dp_sensor_source));

	self->name = name;
	self->interval_ms = 1000;
	dp_output_init(&self->out, DP_DATA_TYPE_FLOAT);
	self->initialized = true;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_sensor_source_free(struct dp_sensor_source *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	dp_output_free(&self->out);

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_sensor_source_start(struct dp_sensor_source *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->can_run = true;
	xTaskCreate(dp_sensor_source_task, "dp-sensor-source", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &self->task);
	if (self->task == NULL) {
		return DATA_PROCESS_RET_FAILED;
	}

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_sensor_source_stop(struct dp_sensor_source *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->running == true)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->can_run = false;
	while (self->running) {
		vTaskDelay(1);
	}

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_sensor_source_set_sensor(struct dp_sensor_source *self, ISensor *sensor) {
	if (u_assert(self != NULL) ||
	    u_assert(sensor != NULL) ||
	    u_assert(self->running == false)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->sensor = sensor;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_sensor_source_interval(struct dp_sensor_source *self, uint32_t interval_ms) {
	if (u_assert(self != NULL) ||
	    u_assert(interval_ms > 0)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->interval_ms = interval_ms;

	return DATA_PROCESS_RET_OK;
}


dp_ret_t dp_sensor_source_set_offset_multiplier(struct dp_sensor_source *self, float offset, float multiplier) {
	if (u_assert(self != NULL)) {
		return DATA_PROCESS_RET_FAILED;
	}

	self->offset = offset;
	self->multiplier = multiplier;

	return DATA_PROCESS_RET_OK;
}
