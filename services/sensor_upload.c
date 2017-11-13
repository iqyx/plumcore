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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "interfaces/tcpip.h"
#include "interfaces/sensor.h"

#include "sensor_upload.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "sensor-upload"

static const char *sensor_upload_states[] = {
	"INIT",
	"SOCKET",
	"CONNECTED",
};


static void sensor_upload_task(void *p) {
	SensorUpload *self = (SensorUpload *)p;

	enum sensor_upload_state old_state = SENSOR_UPLOAD_STATE_INIT;
	while (true) {

		switch (self->state) {
			case SENSOR_UPLOAD_STATE_INIT:
				if (tcpip_create_client_socket(self->tcpip, &(self->socket)) == TCPIP_RET_OK) {
					self->state = SENSOR_UPLOAD_STATE_SOCKET;
					// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("socket created"));
				} else {
					// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("cannot create socket"));
				}
				break;

			case SENSOR_UPLOAD_STATE_SOCKET:
				if (tcpip_socket_connect(self->socket, self->address, self->port) == TCPIP_RET_OK) {
					self->state = SENSOR_UPLOAD_STATE_CONNECTED;
					// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("connected"));
				} else {
					// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("cannot connect"));
				}
				break;

			case SENSOR_UPLOAD_STATE_CONNECTED: {

				for (size_t i = 0; i < SENSOR_UPLOAD_MAX_SENSORS; i++) {
					if (self->sensors[i].used) {
						self->sensors[i].time_from_last_send_ms += 1000;

						if (self->sensors[i].time_from_last_send_ms >= self->sensors[i].interval_ms) {
							char line[32];
							if (self->sensors[i].sensor) {
								float value;
								interface_sensor_value(self->sensors[i].sensor, &value);
								snprintf(line, sizeof(line), "%s=%d\n", self->sensors[i].name, (int32_t)value);
							}
							if (self->sensors[i].power) {
								interface_power_measure(self->sensors[i].power);
								int32_t voltage = interface_power_voltage(self->sensors[i].power);
								int32_t current = interface_power_current(self->sensors[i].power);
								snprintf(line, sizeof(line), "%s=%d,%d\n", self->sensors[i].name, voltage, current);
							}

							size_t written = 0;
							if (tcpip_socket_send(self->socket, line, strlen(line), &written) == TCPIP_RET_DISCONNECTED) {
								self->state = SENSOR_UPLOAD_STATE_SOCKET;
							}

							self->sensors[i].time_from_last_send_ms = 0;
						}
					}
				}

				break;
			}

			default:
				self->state = SENSOR_UPLOAD_STATE_INIT;
				break;
		};
		if (self->state != old_state) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("state %s -> %s"), sensor_upload_states[old_state], sensor_upload_states[self->state]);
		}
		old_state = self->state;

		vTaskDelay(1000);

	}
	vTaskDelete(NULL);
}


sensor_upload_ret_t sensor_upload_init(SensorUpload *self, ITcpIp *tcpip, const char *address, uint16_t port) {
	if (u_assert(self != NULL) ||
	    u_assert(tcpip != NULL) ||
	    u_assert(address != NULL)) {
		return SENSOR_UPLOAD_RET_FAILED;
	}

	memset(self, 0, sizeof(SensorUpload));
	/** @todo init the module descriptor */

	self->state = SENSOR_UPLOAD_STATE_INIT;
	self->tcpip = tcpip;
	self->address = address;
	self->port = port;

	xTaskCreate(sensor_upload_task, "sensor-upload", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return SENSOR_UPLOAD_RET_FAILED;
	}

	return SENSOR_UPLOAD_RET_OK;
}


sensor_upload_ret_t sensor_upload_add_sensor(SensorUpload *self, const char *name, ISensor *sensor, uint32_t interval_ms) {
	if (u_assert(self != NULL) ||
	    u_assert(name != NULL) ||
	    u_assert(sensor != NULL) ||
	    u_assert(interval_ms > 0)) {
		return SENSOR_UPLOAD_RET_FAILED;
	}

	for (size_t i = 0; i < SENSOR_UPLOAD_MAX_SENSORS; i++) {
		if (self->sensors[i].used == false) {
			self->sensors[i].used = true;
			self->sensors[i].interval_ms = interval_ms;
			self->sensors[i].time_from_last_send_ms = 0;
			self->sensors[i].name = name;
			self->sensors[i].sensor = sensor;

			return SENSOR_UPLOAD_RET_OK;
		}
	}

	return SENSOR_UPLOAD_RET_FAILED;
}


sensor_upload_ret_t sensor_upload_add_power_device(SensorUpload *self, const char *name, struct interface_power *power, uint32_t interval_ms) {
	if (u_assert(self != NULL) ||
	    u_assert(name != NULL) ||
	    u_assert(power != NULL) ||
	    u_assert(interval_ms > 0)) {
		return SENSOR_UPLOAD_RET_FAILED;
	}

	for (size_t i = 0; i < SENSOR_UPLOAD_MAX_SENSORS; i++) {
		if (self->sensors[i].used == false) {
			self->sensors[i].used = true;
			self->sensors[i].interval_ms = interval_ms;
			self->sensors[i].time_from_last_send_ms = 0;
			self->sensors[i].name = name;
			self->sensors[i].power = power;

			return SENSOR_UPLOAD_RET_OK;
		}
	}

	return SENSOR_UPLOAD_RET_FAILED;
}
