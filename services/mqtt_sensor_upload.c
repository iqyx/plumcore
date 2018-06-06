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
#include "port.h"

#include "mqtt_sensor_upload.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "mqtt-upload"


static void sensor_upload_task(void *p) {
	MqttSensorUpload *self = (MqttSensorUpload *)p;

	while (true) {


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
			snprintf(topic, sizeof(topic), "%s/%s", self->topic_prefix, name);

			/** @todo this is meh. We are using only a single publish instance
			 *        and the topic is changed for every sensor. */
			self->pub.topic_name = topic;

			char line[32];
			snprintf(line, sizeof(line), "%ld.%03u", (int32_t)value, abs(value * 1000) % 1000);

			queue_publish_send_message(&self->pub, (uint8_t *)line, strlen(line));
			vTaskDelay(200);

		}
		vTaskDelay(self->interval_ms);

	}
	vTaskDelete(NULL);
}


mqtt_sensor_upload_ret_t mqtt_sensor_upload_init(MqttSensorUpload *self, Mqtt *mqtt, const char *topic_prefix, uint32_t interval_ms) {
	if (u_assert(self != NULL) ||
	    u_assert(mqtt != NULL)) {
		return MQTT_SENSOR_UPLOAD_RET_FAILED;
	}

	memset(self, 0, sizeof(MqttSensorUpload));
	self->mqtt = mqtt;
	self->topic_prefix = topic_prefix;
	self->interval_ms = interval_ms;

	xTaskCreate(sensor_upload_task, "mqtt-upload", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return MQTT_SENSOR_UPLOAD_RET_FAILED;
	}

	queue_publish_init(&self->pub, "init", 0);
	mqtt_add_publish(self->mqtt, &self->pub);

	return MQTT_SENSOR_UPLOAD_RET_OK;
}



