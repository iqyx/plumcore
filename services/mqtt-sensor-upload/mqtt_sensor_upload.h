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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "module.h"
#include "interfaces/sensor.h"
#include "services/mqtt-tcpip/mqtt_tcpip.h"

#define SENSOR_UPLOAD_MAX_SENSORS 12

typedef enum {
	MQTT_SENSOR_UPLOAD_RET_OK = 0,
	MQTT_SENSOR_UPLOAD_RET_FAILED = -1,
} mqtt_sensor_upload_ret_t;


typedef struct {
	Module module;

	TaskHandle_t task;
	Mqtt *mqtt;
	QueuePublish pub;
	const char *topic_prefix;
	uint32_t interval_ms;
} MqttSensorUpload;


mqtt_sensor_upload_ret_t mqtt_sensor_upload_init(MqttSensorUpload *self, Mqtt *mqtt, const char *topic_prefix, uint32_t interval_ms);
