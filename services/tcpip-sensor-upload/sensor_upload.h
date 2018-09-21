/*
 * upload sensor data over a tcp/ip connection
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
#include "interface_power.h"


#define SENSOR_UPLOAD_MAX_SENSORS 12

typedef enum {
	SENSOR_UPLOAD_RET_OK = 0,
	SENSOR_UPLOAD_RET_FAILED = -1,
} sensor_upload_ret_t;

enum sensor_upload_state {
	SENSOR_UPLOAD_STATE_INIT = 0,
	SENSOR_UPLOAD_STATE_SOCKET,
	SENSOR_UPLOAD_STATE_CONNECTED,
};

struct sensor_upload_sensor {
	bool used;
	uint32_t interval_ms;
	uint32_t time_from_last_send_ms;
	const char *name;
	ISensor *sensor;
	struct interface_power *power;
};

typedef struct {
	Module module;

	TaskHandle_t task;
	enum sensor_upload_state state;
	ITcpIp *tcpip;
	ITcpIpSocket *socket;
	const char *address;
	uint16_t port;

	struct sensor_upload_sensor sensors[SENSOR_UPLOAD_MAX_SENSORS];

} SensorUpload;


sensor_upload_ret_t sensor_upload_init(SensorUpload *self, ITcpIp *tcpip, const char *address, uint16_t port);
sensor_upload_ret_t sensor_upload_add_sensor(SensorUpload *self, const char *name, ISensor *sensor, uint32_t interval_ms);
sensor_upload_ret_t sensor_upload_add_power_device(SensorUpload *self, const char *name, struct interface_power *power, uint32_t interval_ms);
