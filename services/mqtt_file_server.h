/*
 * Upload/download files using the MQTT protocol
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
#include "services/mqtt_tcpip.h"
#include "sffs.h"


#define MQTT_FILE_SERVER_TOPIC_LEN 64
#define MQTT_FILE_SERVER_SUB_TOPIC "in"
#define MQTT_FILE_SERVER_PUB_TOPIC "out"

typedef enum {
	MQTT_FILE_SERVER_RET_OK = 0,
	MQTT_FILE_SERVER_RET_FAILED = -1,
} mqtt_file_server_ret_t;


typedef struct {
	Module module;

	QueueSubscribe sub;
	char sub_topic[MQTT_FILE_SERVER_TOPIC_LEN];
	QueuePublish pub;
	char pub_topic[MQTT_FILE_SERVER_TOPIC_LEN];
	TaskHandle_t task;
	Mqtt *mqtt;

	struct sffs *fs;

} MqttFileServer;


mqtt_file_server_ret_t mqtt_file_server_init(MqttFileServer *self, Mqtt *mqtt, const char *topic_prefix, struct sffs *fs);

