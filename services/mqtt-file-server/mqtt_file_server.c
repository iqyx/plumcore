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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "services/mqtt-tcpip/mqtt_tcpip.h"
#include "sha2.h"
#include "sffs.h"

#include "mqtt_file_server.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "mqtt-fserver"


static void mqtt_file_server_task(void *p) {
	MqttFileServer *self = (MqttFileServer *)p;
	sha256_ctx sha;
	sha256_init(&sha);
	uint8_t digest[SHA256_DIGEST_SIZE] = {0};
	uint32_t expect_bn = 0;
	struct sffs_file f;

	while (true) {
		uint8_t buf[200];
		size_t len;
		if (queue_subscribe_receive_message(&self->sub, buf, sizeof(buf), &len) == MQTT_RET_OK) {
			if (len > 4 && !memcmp(buf, "open", 4)) {
				char filename[32] = {0};
				if (len > (4 + 32 - 1)) {
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("file name too long"), len - 4);
					continue;
				}
				memcpy(filename, buf + 4, len - 4);
				filename[len - 4] = '\0';
				if (sffs_open(self->fs, &f, filename, SFFS_OVERWRITE) != SFFS_OPEN_OK) {
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot open file '%s'"), filename);
					continue;
				}
				sha256_init(&sha);
				expect_bn = 0;
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("file '%s' opened"), filename);

				uint8_t opened[1] = {'o'};
				queue_publish_send_message(&self->pub, opened, 1);

				continue;
			}

			if (len == 5 && !memcmp(buf, "close", 5)) {
				if (sffs_check_file_opened(&f) != SFFS_CHECK_FILE_OPENED_OK) {
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("file not opened"), len - 4);
					continue;
				}
				sha256_final(&sha, digest);
				u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("end sha256=%02x%02x%02x%02x"), digest[0], digest[1], digest[2], digest[3]);
				sffs_close(&f);
				uint8_t hash[5] = {'h', digest[0], digest[1], digest[2], digest[3]};
				queue_publish_send_message(&self->pub, hash, 5);
				continue;
			}

			if (len <= 70 && len >=6 && !memcmp(buf, "data", 4)) {
				uint32_t bn = (buf[4] << 8) + buf[5];
				uint32_t block_len = len - 6;
				uint8_t confirm[3] = {'c', bn >> 8, bn & 0xff};

				if (bn < expect_bn) {
					/* The same block is received for the second time,
					 * ignore it, but confirm it. */
					queue_publish_send_message(&self->pub, confirm, 3);
					continue;
				} else if (bn > expect_bn) {
					/* Very new block, this should not happen if the new block
					 * is sent only after confirming the old one. */
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("missed %u blocks"), bn - expect_bn);
					expect_bn = bn + 1;
					continue;
				}
				if (sffs_check_file_opened(&f) != SFFS_CHECK_FILE_OPENED_OK) {
					u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("file not opened"), len - 4);
					continue;
				}
				sffs_write(&f, buf + 6, block_len);
				sha256_update(&sha, buf + 6, block_len);
				u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("block num=%u len=%u written"), bn, block_len);
				queue_publish_send_message(&self->pub, confirm, 3);
				expect_bn = bn + 1;
				continue;
			}

		} else {
			vTaskDelay(100);
		}

	}
	vTaskDelete(NULL);
}


mqtt_file_server_ret_t mqtt_file_server_init(MqttFileServer *self, Mqtt *mqtt, const char *topic_prefix, struct sffs *fs) {
	if (u_assert(self != NULL) ||
	    u_assert(mqtt != NULL)) {
		return MQTT_FILE_SERVER_RET_FAILED;
	}

	memset(self, 0, sizeof(MqttFileServer));
	self->mqtt = mqtt;
	self->fs = fs;

	snprintf(self->sub_topic, MQTT_FILE_SERVER_TOPIC_LEN, "%s/%s", topic_prefix, MQTT_FILE_SERVER_SUB_TOPIC);
	queue_subscribe_init(&self->sub, self->sub_topic, 0);
	mqtt_add_subscribe(mqtt, &self->sub);

	snprintf(self->pub_topic, MQTT_FILE_SERVER_TOPIC_LEN, "%s/%s", topic_prefix, MQTT_FILE_SERVER_PUB_TOPIC);
	queue_publish_init(&self->pub, self->pub_topic, 0);
	mqtt_add_publish(mqtt, &self->pub);


	xTaskCreate(mqtt_file_server_task, "mqtt-fserver", configMINIMAL_STACK_SIZE + 384, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return MQTT_FILE_SERVER_RET_FAILED;
	}

	return MQTT_FILE_SERVER_RET_OK;
}


