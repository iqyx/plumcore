/*
 * upload sensor data using the MQTT protocol
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "services/mqtt_tcpip.h"
#include "interfaces/sensor.h"

#include "mqtt_sensor_upload.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "mqtt-upload"


static void sensor_upload_task(void *p) {
	MqttSensorUpload *self = (MqttSensorUpload *)p;

	while (true) {

		for (size_t i = 0; i < SENSOR_UPLOAD_MAX_SENSORS; i++) {
			if (self->sensors[i].used) {
				self->sensors[i].time_from_last_send_ms += 1000;

				if (self->sensors[i].time_from_last_send_ms >= self->sensors[i].interval_ms) {
					char line[32];
					if (self->sensors[i].sensor) {
						float value;
						interface_sensor_value(self->sensors[i].sensor, &value);
						snprintf(line, sizeof(line), "%ld", (int32_t)value);
					}
					queue_publish_send_message(&(self->sensors[i].pub), (uint8_t *)line, strlen(line));

					self->sensors[i].time_from_last_send_ms = 0;
				}
			}
		}

		vTaskDelay(1000);

	}
	vTaskDelete(NULL);
}


mqtt_sensor_upload_ret_t mqtt_sensor_upload_init(MqttSensorUpload *self, Mqtt *mqtt) {
	if (u_assert(self != NULL) ||
	    u_assert(mqtt != NULL)) {
		return MQTT_SENSOR_UPLOAD_RET_FAILED;
	}

	memset(self, 0, sizeof(MqttSensorUpload));

	self->mqtt = mqtt;

	xTaskCreate(sensor_upload_task, "mqtt-upload", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return MQTT_SENSOR_UPLOAD_RET_FAILED;
	}

	return MQTT_SENSOR_UPLOAD_RET_OK;
}


mqtt_sensor_upload_ret_t mqtt_sensor_upload_add_sensor(MqttSensorUpload *self, const char *name, ISensor *sensor, uint32_t interval_ms) {
	if (u_assert(self != NULL) ||
	    u_assert(name != NULL) ||
	    u_assert(sensor != NULL) ||
	    u_assert(interval_ms > 0)) {
		return MQTT_SENSOR_UPLOAD_RET_FAILED;
	}

	for (size_t i = 0; i < SENSOR_UPLOAD_MAX_SENSORS; i++) {
		if (self->sensors[i].used == false) {
			self->sensors[i].used = true;
			queue_publish_init(&(self->sensors[i].pub), name, 0);
			mqtt_add_publish(self->mqtt, &(self->sensors[i].pub));
			self->sensors[i].interval_ms = interval_ms;
			self->sensors[i].time_from_last_send_ms = 0;
			self->sensors[i].sensor = sensor;

			return MQTT_SENSOR_UPLOAD_RET_OK;
		}
	}

	return MQTT_SENSOR_UPLOAD_RET_FAILED;
}


