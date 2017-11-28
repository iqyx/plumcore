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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "u_assert.h"
#include "u_log.h"

#include "interfaces/tcpip.h"
#include "mqtt_tcpip.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "mqtt"

static const char *mqtt_state_strings[] = {
	"NONE",
	"INIT",
	"NO_SOCKET",
	"NET_CONNECTING",
	"PROTO_CONNECTING",
	"CONNECTED",
	"SUBSCRIBE",
	"DISCONNECT",
	"PING",
	"PUBLISH",
};


/* Helper function to get time interval in which the next reconnection attempt
 * should be executed. It performs a basic exponential backoff algo (multiply by 2)
 * to avoid excessive data traffic when the remote broker server does not respond. */
static uint32_t get_reconnect_timeout(Mqtt *self) {
	self->reconnect_timeout_ms *= 2;
	if (self->reconnect_timeout_ms >= MQTT_RECONNECT_TIMEOUT_MAX_MS) {
		self->reconnect_timeout_ms = MQTT_RECONNECT_TIMEOUT_MAX_MS;
	}
	return self->reconnect_timeout_ms;
}


/* When the MQTT protocol is successfully conneceted, return the reconnect interval
 * back to the initial value. */
static void clear_reconnect_timeout(Mqtt *self) {
	self->reconnect_timeout_ms = MQTT_INITIAL_RECONNECT_TIMEOUT_MS;
}


/* Return a new and fresh MQTT packet ID as dictated in the spec. */
static int get_packet_id(Mqtt *self) {
	self->last_packet_id++;
	if (self->last_packet_id >= MQTT_MAX_PACKET_ID) {
		self->last_packet_id = 1;
	}
	return self->last_packet_id;
}


/* Callback functions for the WolfMQTT library (MqttNet) follow. */

static int net_connect(void *context, const char *host, word16 port, int timeout_ms) {
	Mqtt *self = (Mqtt *)context;

	/* We do not need the timeout value as the ITcpIp interface doesn't support
	 * timeout setting yet. */
	(void)timeout_ms;

	if (tcpip_socket_connect(self->socket, host, port) == TCPIP_RET_OK) {
		if (self->debug) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("connected"));
		}
		return MQTT_CODE_SUCCESS;
	}
	return MQTT_CODE_ERROR_NETWORK;
}


static int net_write(void *context, const byte *buf, int buf_len, int timeout_ms) {
	Mqtt *self = (Mqtt *)context;
	/* Ignored, socket write is blocking and waits until all data is written. */
	(void)timeout_ms;

	size_t written;
	while (tcpip_socket_send(self->socket, buf, buf_len, &written) != TCPIP_RET_OK) {
		vTaskDelay(200);
	}

	return written;
}


static int net_read(void *context, byte *buf, int buf_len, int timeout_ms) {
	Mqtt *self = (Mqtt *)context;
	/* Ignored, we cannot set socket read timeout yet. */
	(void)timeout_ms;

	size_t read;
	tcpip_ret_t ret = tcpip_socket_receive(self->socket, buf, buf_len, &read);
	if (ret == TCPIP_RET_OK) {
		return read;
	} else if (ret == TCPIP_RET_NODATA) {
		return MQTT_CODE_CONTINUE;
	} else if (ret == TCPIP_RET_DISCONNECTED) {
		return MQTT_CODE_ERROR_NETWORK;
	}

	/* Handle other errors. */
	return MQTT_CODE_CONTINUE;
}


static int net_disconnect(void *context) {
	Mqtt *self = (Mqtt *)context;

	/* Do not check the return value. */
	tcpip_socket_disconnect(self->socket);
	return MQTT_CODE_SUCCESS;
}


static int mqtt_msg(MqttClient *client, MqttMessage *message, byte msg_new, byte msg_done) {
	Mqtt *self = (Mqtt *)client->ctx;
	(void)msg_new;
	(void)msg_done;

	/** @todo do not convert the topic to  null terminated string, use memcmp instead if strcmp */
	char topic[64];
	size_t len = message->topic_name_len;
	if (len >= sizeof(topic)) {
		len = sizeof(topic) - 1;
	}
	memcpy(topic, message->topic_name, len);
	topic[len] = '\0';

	/* Iterate over all subscriptions and try to match the topic. */
	/** @todo more advanced topic matching */
	QueueSubscribe *q = self->subscribes;
	while (q != NULL) {
		if (!strcmp(topic, q->topic_name)) {
			if (message->total_len <= q->buffer_size) {
				memcpy(q->buffer, message->buffer, message->total_len);
				*(q->data_len) = message->total_len;
				q->ret = MQTT_RET_OK;
			} else {
				q->ret = MQTT_RET_FAILED;
			}
			xSemaphoreGive(q->msg_wait_lock);
		}
		q = q->next;
	}

	if (self->debug) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("message received topic='%s', len=%u"), topic, message->total_len);
	}

	return MQTT_CODE_SUCCESS;
}


/* Performs a single step in the main state machine. */
static mqtt_ret_t mqtt_step(Mqtt *self) {
	enum mqtt_state last_state = self->state;
	switch (self->state) {
		case MQTT_STATE_INIT:
			/* Just wait for the initialization/connection request. */
			vTaskDelay(100);
			break;

		case MQTT_STATE_NO_SOCKET:
			if (tcpip_create_client_socket(self->tcpip, &(self->socket)) == TCPIP_RET_OK) {
				if (self->debug) {
					u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("socket created"));
				}
				self->state = MQTT_STATE_NET_CONNECTING;
			} else {
				/* A new socket cannot be acquired. Either the tcpip interface is
				 * busy or the maximum number of socket has been reached. Wait a bit as we
				 * cannot do anything more without a valid socket. */
				if (self->debug) {
					u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("cannot create socket"));
				}
				/* Wait a constant delay as this step does not create any network traffic. */
				vTaskDelay(1000);
			}
			break;

		case MQTT_STATE_NET_CONNECTING: {

			/* Initialize the MqttNet structure first. */
			self->mqtt_net.context = (void *)self;
			self->mqtt_net.connect = net_connect;
			self->mqtt_net.disconnect = net_disconnect;
			self->mqtt_net.read = net_read;
			self->mqtt_net.write = net_write;

			int rc = MqttClient_Init(&self->mqtt_client, &self->mqtt_net, mqtt_msg,
			                         self->mqtt_tx_buf, MQTT_TX_BUFFER_SIZE,
			                         self->mqtt_rx_buf, MQTT_RX_BUFFER_SIZE,
			                         100
			);
			/* Save the Mqtt instance in the MqttClient structure as a ctx field. This will
			 * be required later in the message callback. */
			self->mqtt_client.ctx = (void *)self;

			rc = MqttClient_NetConnect(&self->mqtt_client, "", 0, 100, false, NULL);
			if (rc == MQTT_CODE_SUCCESS) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("TCP connection to the broker at %s:%u created"), self->address, self->port);
				self->state = MQTT_STATE_PROTO_CONNECTING;
			} else {
				/* Cannot connect the underlying TCP socket. Log the error, wait a bit and repeat. */
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create TCP connection to the broker at %s:%u, '%s'"), self->address, self->port, MqttClient_ReturnCodeToString(rc));
				self->state = MQTT_STATE_DISCONNECT;
			}
			break;
		}

		case MQTT_STATE_PROTO_CONNECTING: {

			/* Prepare the connection parameters. */
			memset(&self->mqtt_connect, 0, sizeof(MqttConnect));
			self->mqtt_connect.keep_alive_sec = self->ping_interval_ms / 1000;
			self->mqtt_connect.clean_session = 1;
			self->mqtt_connect.client_id = self->client_id;
			self->mqtt_connect.enable_lwt = 0;

			/* Repeat the call to process all received data. */
			int rc = 0;
			rc = MqttClient_Connect(&self->mqtt_client, &self->mqtt_connect);
			if (rc == MQTT_CODE_CONTINUE) {
				/* Try to connect again. */
				break;
			} else if (rc == MQTT_CODE_SUCCESS) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("MQTT protocol connected successfully"));
				self->time_from_last_ping_ms = 0;

				/* Mark all existing subscriptions as new to correctly resubscribe. */
				QueueSubscribe *q = self->subscribes;
				while (q != NULL) {
					q->subscribed = false;
					q = q->next;
				}
				self->new_subscription = true;

				self->state = MQTT_STATE_CONNECTED;
			} else {
				/* If there is a protocol error while trying to connect, disconnect the socket and
				 * try to connect again. */
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("MQTT protocol connection error '%s'"), MqttClient_ReturnCodeToString(rc));
				self->state = MQTT_STATE_DISCONNECT;
			}
			break;
		}

		case MQTT_STATE_SUBSCRIBE: {

			QueueSubscribe *q = self->subscribes;
			while (q != NULL && q->subscribed == true) {
				q = q->next;
			}
			if (q == NULL || q->subscribed == true) {
				/* No new subscriptions found. */
				self->new_subscription = false;
				self->state = MQTT_STATE_CONNECTED;
				break;
			}

			MqttTopic topics_to_subscribe[1];
			memset(topics_to_subscribe, 0, sizeof(topics_to_subscribe));
			topics_to_subscribe[0].qos = q->qos;
			topics_to_subscribe[0].topic_filter = q->topic_name;

			MqttSubscribe sub;
			memset(&sub, 0, sizeof(sub));
			sub.topic_count = 1;
			sub.topics = topics_to_subscribe;
			sub.packet_id = get_packet_id(self);

			/* We cannot simply return in case of rc == CONTINUE because the next call of the MqttClient_Subscribe
			 * function must have the same parameters. We try multiple times instead with the same sub structure
			 * and return success/fail afterwards. */
			uint32_t count = 0;
			int rc = 0;
			while ((rc = MqttClient_Subscribe(&self->mqtt_client, &sub)) == MQTT_CODE_CONTINUE) {
				count++;
				if (count >= 5) {
					break;
				}
			}
			if (rc == MQTT_CODE_SUCCESS) {
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("subscribed to a topic name='%s', qos=%d"), sub.topics[0].topic_filter, sub.topics[0].qos);
				q->subscribed = true;
				self->state = MQTT_STATE_CONNECTED;
			} else {
				/* We were not able to subscribe to a topic. Try again. */
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot subscribe to a topic name='%s', qos=%d, '%s'"), sub.topics[0].topic_filter, sub.topics[0].qos, MqttClient_ReturnCodeToString(rc));
			}
			break;
		}


		case MQTT_STATE_CONNECTED: {
			clear_reconnect_timeout(self);

			/* Check if a MQTT Ping needs to be sent. */
			if (self->time_from_last_ping_ms >= self->ping_interval_ms) {
				self->state = MQTT_STATE_PING;
				break;
			}

			/* If there is a newly added subscription, process it. */
			if (self->new_subscription) {
				self->state = MQTT_STATE_SUBSCRIBE;
				break;
			}

			/* If a new message has to be published, publish it. */
			QueuePublish *q = self->publishes;
			while (q != NULL) {
				if (q->new_message) {
					self->state = MQTT_STATE_PUBLISH;
					break;
				}
				q = q->next;
			}

			/* Otherwise wait for a message. */
			int rc = MqttClient_WaitMessage(&self->mqtt_client, 100);
			self->time_from_last_ping_ms += 100;
			if (rc == MQTT_CODE_CONTINUE) {
				break;
			} else if (rc == MQTT_CODE_ERROR_NETWORK) {
				self->state = MQTT_STATE_DISCONNECT;
			}

			break;
		}

		case MQTT_STATE_DISCONNECT: {
			tcpip_socket_disconnect(self->socket);
			vTaskDelay(get_reconnect_timeout(self));
			self->state = MQTT_STATE_NET_CONNECTING;
			break;
		}

		case MQTT_STATE_PING: {
			int rc = MqttClient_Ping(&self->mqtt_client);
			if (rc == MQTT_CODE_CONTINUE) {
				break;
			}
			if (rc == MQTT_CODE_SUCCESS) {
				if (self->debug) {
					u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("MQTT ping '%s'"), MqttClient_ReturnCodeToString(rc));
				}
				self->time_from_last_ping_ms = 0;
				self->state = MQTT_STATE_CONNECTED;
			} else {
				/* Error during ping processing. Disconnect just in case something goes wrong. */
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot ping the broker, disconnecting '%s'"), MqttClient_ReturnCodeToString(rc));
				self->state = MQTT_STATE_DISCONNECT;
			}
			break;
		}

		case MQTT_STATE_PUBLISH: {
			/* Find the first message ready to be sent. */
			QueuePublish *q = self->publishes;
			while (q != NULL) {
				if (q->new_message) {
					break;
				}
				q = q->next;
			}
			if (q == NULL) {
				/* We did not find any message to send. */
				self->state = MQTT_STATE_CONNECTED;
				break;
			}

			MqttPublish msg;
			memset(&msg, 0, sizeof(MqttPublish));
			msg.retain = 0;
			msg.qos = q->qos;
			msg.duplicate = 0;
			msg.topic_name = q->topic_name;
			msg.packet_id = get_packet_id(self);
			msg.buffer = q->buffer;
			msg.total_len = q->data_len;

			int rc = MqttClient_Publish(&self->mqtt_client, &msg);
			if (rc == MQTT_CODE_SUCCESS) {
				if (self->debug) {
					u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("published to topic name='%s', qos=%d"), msg.topic_name, msg.qos);
				}
				q->new_message = false;
				q->ret = MQTT_RET_OK;
				xSemaphoreGive(q->msg_sent_lock);
				self->state = MQTT_STATE_CONNECTED;
			} else {
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot publish message to name='%s', qos=%d, '%s'"), msg.topic_name, msg.qos, MqttClient_ReturnCodeToString(rc));
				q->ret = MQTT_RET_FAILED;
				xSemaphoreGive(q->msg_sent_lock);
				self->state = MQTT_STATE_CONNECTED;
			}
			break;
		}

		default:
			return MQTT_RET_FAILED;
	}

	if (last_state == self->state) {
		self->state_time++;
	} else {
		self->state_time = 0;
		if (self->debug) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("state '%s' -> '%s'"), mqtt_state_strings[last_state], mqtt_state_strings[self->state]);
		}
	}

	return MQTT_RET_OK;
}


static void mqtt_task(void *p) {
	Mqtt *self = (Mqtt *)p;

	while (self->can_run) {
		mqtt_step(self);
		if (self->state_time >= MQTT_STATE_TIMEOUT && self->state != MQTT_STATE_CONNECTED && self->state != MQTT_STATE_NO_SOCKET && self->state != MQTT_STATE_INIT) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("state timeout %u"), self->state_time);
			self->state = MQTT_STATE_DISCONNECT;
			self->state_time = 0;
		}
	}

	vTaskDelete(NULL);
}


mqtt_ret_t mqtt_init(Mqtt *self, ITcpIp *tcpip) {
	if (u_assert(self != NULL) ||
	    u_assert(tcpip != NULL)) {
		return MQTT_RET_FAILED;
	}

	memset(self, 0, sizeof(Mqtt));
	self->tcpip = tcpip;
	self->client_id = "noname";
	clear_reconnect_timeout(self);
	self->ping_interval_ms = 60000;
	self->debug = false;
	self->state = MQTT_STATE_INIT;

	self->can_run = true;
	xTaskCreate(mqtt_task, "mqtt", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return MQTT_RET_FAILED;
	}

	return MQTT_RET_OK;
}


mqtt_ret_t mqtt_free(Mqtt *self) {
	if (u_assert(self != NULL)) {
		return MQTT_RET_FAILED;
	}

	/* Delete the task. It may take some time. Return immediately. */
	self->can_run = false;

	return MQTT_RET_OK;
}


mqtt_ret_t mqtt_connect(Mqtt *self, const char *address, uint16_t port) {
	if (u_assert(self != NULL) ||
	    u_assert(address != NULL)) {
		return MQTT_RET_FAILED;
	}

	if (self->state != MQTT_STATE_INIT) {
		return MQTT_WRONG_STATE;
	}

	self->address = address;
	self->port = port;
	self->state = MQTT_STATE_NO_SOCKET;

	return MQTT_RET_OK;
}


mqtt_ret_t mqtt_set_client_id(Mqtt *self, const char *client_id) {
	if (u_assert(self != NULL) ||
	    u_assert(client_id != NULL)) {
		return MQTT_RET_FAILED;
	}

	if (self->state != MQTT_STATE_INIT) {
		return MQTT_WRONG_STATE;
	}

	self->client_id = client_id;

	return MQTT_RET_OK;
}


mqtt_ret_t mqtt_set_ping_interval(Mqtt *self, uint32_t ping_interval_ms) {
	if (u_assert(self != NULL)) {
		return MQTT_RET_FAILED;
	}

	if (self->state != MQTT_STATE_INIT) {
		return MQTT_WRONG_STATE;
	}

	self->ping_interval_ms = ping_interval_ms;

	return MQTT_RET_OK;
}


mqtt_ret_t mqtt_add_subscribe(Mqtt *self, QueueSubscribe *subscribe) {
	if (u_assert(self != NULL) ||
	    u_assert(subscribe != NULL)) {
		return MQTT_RET_FAILED;
	}

	subscribe->next = self->subscribes;
	subscribe->parent = self;
	subscribe->subscribed = false;
	self->subscribes = subscribe;
	self->new_subscription = true;

	return MQTT_RET_OK;
}


mqtt_ret_t queue_subscribe_init(QueueSubscribe *self, const char *topic_name, int qos) {
	if (u_assert(self != NULL) ||
	    u_assert(topic_name != NULL)) {
		return MQTT_RET_FAILED;
	}

	memset(self, 0, sizeof(QueueSubscribe));
	self->topic_name = topic_name;
	self->qos = qos;
	self->msg_wait_lock = xSemaphoreCreateBinary();
	if (self->msg_wait_lock == NULL) {
		return MQTT_RET_FAILED;
	}

	return MQTT_RET_OK;
}


mqtt_ret_t queue_subscribe_receive_message(QueueSubscribe *self, uint8_t *buffer, size_t buffer_size, size_t *data_len) {
	if (u_assert(self != NULL) ||
	    u_assert(buffer != NULL) ||
	    u_assert(data_len != NULL)) {
		return MQTT_RET_FAILED;
	}

	self->buffer = buffer;
	self->buffer_size = buffer_size;
	self->data_len = data_len;
	xSemaphoreTake(self->msg_wait_lock, portMAX_DELAY);
	return self->ret;
}


mqtt_ret_t mqtt_add_publish(Mqtt *self, QueuePublish *publish) {
	if (u_assert(self != NULL) ||
	    u_assert(publish != NULL)) {
		return MQTT_RET_FAILED;
	}

	publish->next = self->publishes;
	publish->parent = self;
	publish->new_message = false;
	self->publishes = publish;

	return MQTT_RET_OK;
}


mqtt_ret_t queue_publish_init(QueuePublish *self, const char *topic_name, int qos) {
	if (u_assert(self != NULL) ||
	    u_assert(topic_name != NULL)) {
		return MQTT_RET_FAILED;
	}

	memset(self, 0, sizeof(QueuePublish));
	self->topic_name = topic_name;
	self->qos = qos;
	self->msg_sent_lock = xSemaphoreCreateBinary();
	if (self->msg_sent_lock == NULL) {
		return MQTT_RET_FAILED;
	}

	return MQTT_RET_OK;
}


mqtt_ret_t queue_publish_send_message(QueuePublish *self, uint8_t *buffer, size_t data_len) {
	if (u_assert(self != NULL) ||
	    u_assert(buffer != NULL)) {
		return MQTT_RET_FAILED;
	}

	self->buffer = buffer;
	self->data_len = data_len;
	self->new_message = true;

	xSemaphoreTake(self->msg_sent_lock, portMAX_DELAY);
	return self->ret;

}
