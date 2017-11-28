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
#include "mqtt_tcpip.h"
#include "interface_stream.h"


#define STREAM_OVER_MQTT_TX_QUEUE_LEN 128
#define STREAM_OVER_MQTT_RX_QUEUE_LEN 128
#define STREAM_OVER_MQTT_TOPIC_LEN 64
#define STREAM_OVER_MQTT_SUB_TOPIC "in"
#define STREAM_OVER_MQTT_PUB_TOPIC "out"


typedef enum {
	STREAM_OVER_MQTT_RET_OK = 0,
	STREAM_OVER_MQTT_RET_FAILED = -1,
	STREAM_OVER_MQTT_RET_BAD_STATE = -2,
} stream_over_mqtt_ret_t;


enum stream_over_mqtt_state {
	STREAM_OVER_MQTT_STATE_NONE = 0,
	STREAM_OVER_MQTT_STATE_STOPPED,
	STREAM_OVER_MQTT_STATE_RUNNING,
	STREAM_OVER_MQTT_STATE_STOP_REQ,
};


typedef struct {
	/* State of the service. */
	enum stream_over_mqtt_state state;

	/* Dependencies. */
	Mqtt *mqtt;
	int32_t qos;

	QueueSubscribe sub;
	char sub_topic[STREAM_OVER_MQTT_TOPIC_LEN];

	QueuePublish pub;
	char pub_topic[STREAM_OVER_MQTT_TOPIC_LEN];

	/* Input/output buffers. */
	uint8_t line_buffer[STREAM_OVER_MQTT_TX_QUEUE_LEN];
	size_t line_buffer_len;
	struct interface_stream stream;
	QueueHandle_t rxqueue;

	TaskHandle_t receive_task;

} StreamOverMqtt;


stream_over_mqtt_ret_t stream_over_mqtt_init(StreamOverMqtt *self, Mqtt *mqtt, const char *topic_prefix, int32_t qos);
stream_over_mqtt_ret_t stream_over_mqtt_free(StreamOverMqtt *self);
stream_over_mqtt_ret_t stream_over_mqtt_start(StreamOverMqtt *self);

