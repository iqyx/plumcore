/*
 * MQTT client service
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
#include "module.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "interfaces/tcpip.h"
#include "wolfmqtt/mqtt_socket.h"
#include "wolfmqtt/mqtt_client.h"


#define MQTT_RX_BUFFER_SIZE 128
#define MQTT_TX_BUFFER_SIZE 128
#define MQTT_MAX_PACKET_ID 65535
#define MQTT_INITIAL_RECONNECT_TIMEOUT_MS 1000
#define MQTT_RECONNECT_TIMEOUT_MAX_MS 30000
#define MQTT_MAX_TOPICS_TO_SUBSCRIBE 1
#define MQTT_STATE_TIMEOUT 100


struct mqtt;

typedef enum {
	MQTT_RET_OK = 0,
	MQTT_RET_FAILED = -1,
	MQTT_WRONG_STATE = -2,
} mqtt_ret_t;

enum mqtt_state {
	MQTT_STATE_NONE = 0,
	MQTT_STATE_INIT,
	MQTT_STATE_NO_SOCKET,
	MQTT_STATE_NET_CONNECTING,
	MQTT_STATE_PROTO_CONNECTING,
	MQTT_STATE_CONNECTED,
	MQTT_STATE_SUBSCRIBE,
	MQTT_STATE_DISCONNECT,
	MQTT_STATE_PING,
	MQTT_STATE_PUBLISH,
};


typedef struct queue_subscribe {
	const char *topic_name;
	int qos;
	volatile bool subscribed;
	struct mqtt *parent;
	SemaphoreHandle_t msg_wait_lock;

	uint8_t *buffer;
	size_t buffer_size;
	size_t *data_len;
	mqtt_ret_t ret;

	struct queue_subscribe *next;
} QueueSubscribe;


typedef struct queue_publish {
	const char *topic_name;
	int qos;
	volatile bool new_message;
	struct mqtt *parent;
	SemaphoreHandle_t msg_sent_lock;
	uint8_t *buffer;
	size_t data_len;
	mqtt_ret_t ret;

	struct queue_publish *next;
} QueuePublish;


typedef struct mqtt {
	Module module;

	enum mqtt_state state;
	uint32_t state_time;

	const char *address;
	uint16_t port;
	const char *client_id;
	uint32_t reconnect_timeout_ms;
	uint32_t time_from_last_ping_ms;
	uint32_t ping_interval_ms;

	TaskHandle_t task;
	ITcpIp *tcpip;
	ITcpIpSocket *socket;
	MqttNet mqtt_net;
	uint8_t mqtt_rx_buf[MQTT_RX_BUFFER_SIZE];
	uint8_t mqtt_tx_buf[MQTT_TX_BUFFER_SIZE];
	MqttClient mqtt_client;
	MqttConnect mqtt_connect;
	int last_packet_id;

	MqttTopic topics_to_subscribe[MQTT_MAX_TOPICS_TO_SUBSCRIBE];
	size_t topics_to_subscribe_count;

	QueueSubscribe *subscribes;
	bool new_subscription;

	QueuePublish *publishes;

} Mqtt;


mqtt_ret_t mqtt_init(Mqtt *self, ITcpIp *tcpip);
mqtt_ret_t mqtt_free(Mqtt *self);
mqtt_ret_t mqtt_connect(Mqtt *self, const char *address, uint16_t port);
mqtt_ret_t mqtt_set_client_id(Mqtt *self, const char *client_id);
mqtt_ret_t mqtt_set_ping_interval(Mqtt *self, uint32_t ping_interval_ms);

mqtt_ret_t mqtt_add_subscribe(Mqtt *self, QueueSubscribe *subscribe);
mqtt_ret_t queue_subscribe_init(QueueSubscribe *self, const char *topic_name, int qos);
mqtt_ret_t queue_subscribe_receive_message(QueueSubscribe *self, uint8_t *buffer, size_t buffer_size, size_t *data_len);

mqtt_ret_t mqtt_add_publish(Mqtt *self, QueuePublish *publish);
mqtt_ret_t queue_publish_init(QueuePublish *self, const char *topic_name, int qos);
mqtt_ret_t queue_publish_send_message(QueuePublish *self, uint8_t *buffer, size_t data_len);
