/*
 * A service for uploading sensor values to the plog message router
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include "interfaces/plog/descriptor.h"
#include "interfaces/plog/client.h"
#include "interfaces/sensor.h"

#include "plog_sensor_upload.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "plog-sensor"


static void plog_sensor_upload_task(void *p) {
	PlogSensorUpload *self = (PlogSensorUpload *)p;

	self->running = true;
	while (self->can_run) {
		Interface *interface;
		for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_SENSOR, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
			ISensor *sensor = (ISensor *)interface;
			ISensorInfo info = {0};
			const char *name = "";
			float value;
			iservicelocator_get_name(locator, interface, &name);
			interface_sensor_info(sensor, &info);
			interface_sensor_value(sensor, &value);

			char topic[32] = {0};
			snprintf(topic, sizeof(topic), "sensor/%s", name);
			plog_publish_float(&self->plog, topic, value);
		}
		vTaskDelay(self->interval_ms);
	}
	vTaskDelete(NULL);
	self->running = false;
}


plog_sensor_upload_ret_t plog_sensor_upload_init(PlogSensorUpload *self, uint32_t interval_ms) {
	if (u_assert(self != NULL)) {
		return PLOG_SENSOR_UPLOAD_RET_NULL;
	}

	memset(self, 0, sizeof(PlogSensorUpload));
	self->interval_ms = interval_ms;

	/* Discover the plog router interface and create one client. */
	Interface *interface;
	if (iservicelocator_query_name_type(locator, "plog-router", ISERVICELOCATOR_TYPE_PLOG_ROUTER, &interface) != ISERVICELOCATOR_RET_OK) {
		return PLOG_SENSOR_UPLOAD_RET_FAILED;
	}
	IPlog *iplog = (IPlog *)interface;

	if (plog_open(&self->plog, iplog) != PLOG_RET_OK) {
		return PLOG_SENSOR_UPLOAD_RET_FAILED;
	}

	self->can_run = true;
	xTaskCreate(plog_sensor_upload_task, "plog-sensor", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return PLOG_SENSOR_UPLOAD_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("plog sensor upload service started"));
	self->initialized = true;
	return PLOG_SENSOR_UPLOAD_RET_OK;
}


plog_sensor_upload_ret_t plog_sensor_upload_free(PlogSensorUpload *self) {
	if (self == NULL) {
		return PLOG_SENSOR_UPLOAD_RET_NULL;
	}

	if (self->initialized == false) {
		return PLOG_SENSOR_UPLOAD_RET_FAILED;
	}

	self->can_run = false;
	while (self->running) {
		vTaskDelay(10);
	}

	plog_close(&self->plog);
	self->initialized = false;
	return PLOG_SENSOR_UPLOAD_RET_OK;
}
