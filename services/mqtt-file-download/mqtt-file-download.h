/*
 * Implementation of a stream interface over the MQTT protocol
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

#include "FreeRTOS.h"
#include "queue.h"
#include "services/mqtt-tcpip/mqtt_tcpip.h"


typedef enum {
	MQTT_FILE_DOWNLOAD_RET_OK = 0,
	MQTT_FILE_DOWNLOAD_RET_FAILED = -1,
	MQTT_FILE_DOWNLOAD_RET_BAD_STATE = -2,
} mqtt_file_download_ret_t;


typedef struct {
	/* Dependencies. */
	Mqtt *mqtt;
	int32_t qos;

	QueueSubscribe sub;
	char sub_topic[64];

	TaskHandle_t receive_task;

} MqttFileDownload;


mqtt_file_download_ret_t mqtt_file_download_init(MqttFileDownload *self, Mqtt *mqtt);
mqtt_file_download_ret_t mqtt_file_download_free(MqttFileDownload *self);

