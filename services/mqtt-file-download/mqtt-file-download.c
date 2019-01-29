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
#include "services/mqtt-tcpip/mqtt_tcpip.h"
#include "mqtt-file-download.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "mqtt-file-download"


static void mqtt_file_download_receive_task(void *p) {
	MqttFileDownload *self = (MqttFileDownload *)p;

	while (true) {
		uint8_t buf[64];
		size_t len;
		if (queue_subscribe_receive_message(&self->sub, buf, sizeof(buf), &len) == MQTT_RET_OK) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("received topic %s"), self->sub.msg_topic);
			/* Process buffer here. */
		}
	}

	vTaskDelete(NULL);
}


mqtt_file_download_ret_t mqtt_file_download_init(MqttFileDownload *self, Mqtt *mqtt) {
	if (u_assert(self != NULL) ||
	    u_assert(mqtt != NULL)) {
		return MQTT_FILE_DOWNLOAD_RET_FAILED;
	}

	memset(self, 0, sizeof(MqttFileDownload));

	/* Fill in the dependencies first. */
	self->mqtt = mqtt;

	strcpy(self->sub_topic, "test/file/filename/#");
	queue_subscribe_init(&self->sub, self->sub_topic, 0);
	mqtt_add_subscribe(mqtt, &self->sub);

	xTaskCreate(mqtt_file_download_receive_task, "mqtt-download", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &self->receive_task);
	if (self->receive_task == NULL) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("initialized"));
	return MQTT_FILE_DOWNLOAD_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("initialization failed"));
	return MQTT_FILE_DOWNLOAD_RET_FAILED;
}


mqtt_file_download_ret_t mqtt_file_download_free(MqttFileDownload *self) {
	if (u_assert(self != NULL)) {
		return MQTT_FILE_DOWNLOAD_RET_FAILED;
	}

	return MQTT_FILE_DOWNLOAD_RET_OK;
}


