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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

/** @todo using service headers directly is prohibited. Replace with an interface header. */
#include "mqtt_tcpip.h"
#include "stream_over_mqtt.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "stream-mqtt"


static void stream_over_mqtt_receive_task(void *p) {
	StreamOverMqtt *self = (StreamOverMqtt *)p;

	while (self->state == STREAM_OVER_MQTT_STATE_RUNNING) {
		uint8_t buf[64];
		size_t len;
		if (queue_subscribe_receive_message(&self->sub, buf, sizeof(buf), &len) == MQTT_RET_OK) {
			for (size_t i = 0; i < len; i++) {
				xQueueSendToBack(self->rxqueue, &(buf[i]), 0);
			}
			uint8_t end = '\n';
			xQueueSendToBack(self->rxqueue, &end, 0);
		}
	}

	self->state = STREAM_OVER_MQTT_STATE_STOPPED;
	vTaskDelete(NULL);
}


static int32_t mqtt_cli_read_timeout(void *context, uint8_t *buf, uint32_t len, uint32_t timeout) {
	if (u_assert(context != NULL && len > 0)) {
		return -1;
	}
	StreamOverMqtt *self = (StreamOverMqtt *)context;

	/* Wait for at least one character. */
	if (xQueuePeek(self->rxqueue, &buf[0], timeout) == pdFALSE) {
		return 0;
	}

	uint32_t read = 0;
	while (read < len && xQueueReceive(self->rxqueue, &buf[read], 0) == pdTRUE) {
		read++;
	}

	return read;
}


static int32_t mqtt_cli_read(void *context, uint8_t *buf, uint32_t len) {

	return mqtt_cli_read_timeout(context, buf, len, portMAX_DELAY);
}


static int32_t mqtt_cli_write(void *context, const uint8_t *buf, uint32_t len) {
	if (u_assert(context != NULL)) {
		return -1;
	}
	StreamOverMqtt *self = (StreamOverMqtt *)context;

	size_t i = 0;
	while (i < len) {
		if (buf[i] == '\n' || self->line_buffer_len >= STREAM_OVER_MQTT_TX_QUEUE_LEN) {
			queue_publish_send_message(&self->pub, self->line_buffer, self->line_buffer_len);
			self->line_buffer_len = 0;
			/* Eat the newline. */
			i++;
			continue;
		}
		if (self->line_buffer_len <= STREAM_OVER_MQTT_TX_QUEUE_LEN) {
			self->line_buffer[self->line_buffer_len] = buf[i];
			self->line_buffer_len++;
		}
		i++;
	}

	return -1;
}


stream_over_mqtt_ret_t stream_over_mqtt_init(StreamOverMqtt *self, Mqtt *mqtt, const char *topic_prefix, int32_t qos) {
	if (u_assert(self != NULL) ||
	    u_assert(mqtt != NULL)) {
		return STREAM_OVER_MQTT_RET_FAILED;
	}

	memset(self, 0, sizeof(StreamOverMqtt));
	self->state = STREAM_OVER_MQTT_STATE_NONE;

	/* Fill in the dependencies first. */
	self->mqtt = mqtt;
	self->qos = qos;

	/* Init the provided stream interface. */
	interface_stream_init(&self->stream);
	self->stream.vmt.context = (void *)self;
	self->stream.vmt.read = mqtt_cli_read;
	self->stream.vmt.read_timeout = mqtt_cli_read_timeout;
	self->stream.vmt.write = mqtt_cli_write;
	/** @todo advertise the interface using the system service locator */

	snprintf(self->sub_topic, STREAM_OVER_MQTT_TOPIC_LEN, "%s/%s", topic_prefix, STREAM_OVER_MQTT_SUB_TOPIC);
	self->sub_topic[STREAM_OVER_MQTT_TOPIC_LEN - 1] = '\0';
	queue_subscribe_init(&self->sub, self->sub_topic, self->qos);
	mqtt_add_subscribe(mqtt, &self->sub);

	snprintf(self->pub_topic, STREAM_OVER_MQTT_TOPIC_LEN, "%s/%s", topic_prefix, STREAM_OVER_MQTT_PUB_TOPIC);
	self->pub_topic[STREAM_OVER_MQTT_TOPIC_LEN - 1] = '\0';
	queue_publish_init(&self->pub, self->pub_topic, self->qos);
	mqtt_add_publish(mqtt, &self->pub);

	/* Init input/output buffers. */
	self->rxqueue = xQueueCreate(STREAM_OVER_MQTT_RX_QUEUE_LEN, sizeof(uint8_t));
	if (self->rxqueue == NULL) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("initialized"));
	self->state = STREAM_OVER_MQTT_STATE_STOPPED;
	return STREAM_OVER_MQTT_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("initialization failed"));
	return STREAM_OVER_MQTT_RET_FAILED;
}


stream_over_mqtt_ret_t stream_over_mqtt_free(StreamOverMqtt *self) {
	if (u_assert(self != NULL)) {
		return STREAM_OVER_MQTT_RET_FAILED;
	}

	if (self->state != STREAM_OVER_MQTT_STATE_STOPPED) {
		return STREAM_OVER_MQTT_RET_BAD_STATE;
	}

	/** @todo free the resources */
	self->state = STREAM_OVER_MQTT_STATE_NONE;
	return STREAM_OVER_MQTT_RET_OK;
}


stream_over_mqtt_ret_t stream_over_mqtt_start(StreamOverMqtt *self) {
	if (u_assert(self != NULL)) {
		return STREAM_OVER_MQTT_RET_FAILED;
	}

	if (self->state != STREAM_OVER_MQTT_STATE_STOPPED) {
		return STREAM_OVER_MQTT_RET_BAD_STATE;
	}

	xTaskCreate(stream_over_mqtt_receive_task, "stream-mqtt-s", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->receive_task);
	if (self->receive_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return STREAM_OVER_MQTT_RET_FAILED;
	}
	self->state = STREAM_OVER_MQTT_STATE_RUNNING;
	return STREAM_OVER_MQTT_RET_OK;
}


