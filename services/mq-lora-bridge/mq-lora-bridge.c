/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ -> LoRaWAN bridge
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include <main.h>
#include <message_buffer.h>

#include <interfaces/mq.h>
#include <types/ndarray.h>
#include <interfaces/lora.h>

#include "mq-lora-bridge.h"

#define MODULE_NAME "mq-lora-bridge"


static void pbuf_prepare(MqLoraBridge *self) {
	cbor_encoder_init(&self->encoder, self->pbuf, self->pbuf_size, 0);
	cbor_encoder_create_map(&self->encoder, &self->encoder_map, CborIndefiniteLength);
}


static void pbuf_append_timestamp(MqLoraBridge *self, struct timespec *ts) {
	cbor_encode_text_stringz(&self->encoder_map, "ts");
	cbor_encode_uint(&self->encoder_map, ts->tv_sec);
}


static void pbuf_finish(MqLoraBridge *self) {
	cbor_encoder_close_container(&self->encoder, &self->encoder_map);
	size_t len = cbor_encoder_get_buffer_size(&self->encoder, self->pbuf);
	size_t ret = xMessageBufferSend(self->packet_queue, self->pbuf, len, 0);
	if (ret == 0) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("not enough space in packet queue"));
	}
}


static void pbuf_append(MqLoraBridge *self, const char *key, NdArray *d) {
	cbor_encode_text_stringz(&self->encoder_map, key);

	if (d->dtype == DTYPE_FLOAT && d->asize == 1) {
		/** @todo convert float into int in a configurable manner (or, into a fixed point number) */
		cbor_encode_int(&self->encoder_map, (int32_t)(1000.0 * (*(float *)d->buf)));
	} else if (d->dtype == DTYPE_UINT32 && d->asize == 1) {
		cbor_encode_uint(&self->encoder_map, *(uint32_t *)d->buf);
	} else if (d->dtype == DTYPE_INT32 && d->asize == 1) {
		cbor_encode_int(&self->encoder_map, *(int32_t *)d->buf);
	} else {
		cbor_encode_null(&self->encoder_map);
	}
}


static void collect_task(void *p) {
	MqLoraBridge *self = (MqLoraBridge *)p;

	self->collect_task_running = true;
	while (self->can_run) {
		struct timespec ts = {0};
		char topic[MQ_LORA_BRIDGE_MAX_TOPIC_LEN] = {0};
		if (self->mqc->vmt->receive(self->mqc, topic, MQ_LORA_BRIDGE_MAX_TOPIC_LEN, &self->collect_buf, &ts) == MQ_RET_OK) {

			struct mq_lora_bridge_topic *t = self->topic_list;
			while (t) {
				if (!strcmp(topic, t->topic)) {
					t->decimate_counter++;
					if (t->decimate_counter >= t->decimate) {
						/* Consider this message */
						pbuf_append(self, t->key, &self->collect_buf);

						if (t->this_message_triggers) {
							pbuf_append_timestamp(self, &ts);
							pbuf_finish(self);
							pbuf_prepare(self);
						}

						t->decimate_counter = 0;
					}
				}
				t = t->next;
			}
		}
	}
	self->collect_task_running = false;
	vTaskDelete(NULL);
}


static void byte2hex(const uint8_t *in, size_t in_size, char *out) {
	static const char a[] = "0123456789abcdef";

	for (size_t i = 0; i < in_size; i++) {
		out[i * 2] = a[in[i] >> 4];
		out[i * 2 + 1] = a[in[i] & 0xf];
	}
}


static void send_task(void *p) {
	MqLoraBridge *self = (MqLoraBridge *)p;

	self->send_task_running = true;
	while (self->can_run) {
		size_t len = xMessageBufferReceive(self->packet_queue, self->send_pbuf, self->pbuf_size, portMAX_DELAY);
		if (len > 0) {
			/* Convert to a hex string, ensure there is a terminating null at the end. */
			memset(self->hex_pbuf, 0, self->pbuf_size * 2 + 1);
			byte2hex(self->send_pbuf, len, self->hex_pbuf);

			/* And send it over LoRaWAN */
			lora_ret_t ret = self->lora->vmt->send(self->lora, 1, (uint8_t *)self->hex_pbuf, strlen(self->hex_pbuf));
			if (ret != LORA_RET_OK) {
				/** @todo do something if the packet was not sent */
			}
		}
		vTaskDelay(10);
	}
	self->send_task_running = false;
	vTaskDelete(NULL);
}


mq_lora_bridge_ret_t mq_lora_bridge_init(MqLoraBridge *self, Mq *mq, LoRa *lora) {
	memset(self, 0, sizeof(MqLoraBridge));
	self->mq = mq;
	self->lora = lora;

	return MQ_LORA_BRIDGE_RET_OK;
}


mq_lora_bridge_ret_t mq_lora_bridge_free(MqLoraBridge *self) {
	(void)self;

	return MQ_LORA_BRIDGE_RET_OK;
}


mq_lora_bridge_ret_t mq_lora_bridge_start(MqLoraBridge *self, size_t mq_size, size_t packet_size, size_t queue_len) {
	/* Connecto to the message queue */
	self->mqc = self->mq->vmt->open(self->mq);
	if (self->mqc == NULL) {
		goto err;
	}

	/* Create a buffer for receiving MQ messages */
	if (ndarray_init_empty(&self->collect_buf, DTYPE_BYTE, mq_size) != NDARRAY_RET_OK) {
		goto err;
	}

	self->pbuf = malloc(packet_size);
	if (self->pbuf == NULL) {
		goto err;
	}
	self->pbuf_size = packet_size;

	self->send_pbuf = malloc(packet_size);
	if (self->send_pbuf == NULL) {
		goto err;
	}

	self->hex_pbuf = malloc(packet_size * 2 + 1);
	if (self->hex_pbuf == NULL) {
		goto err;
	}


	self->list_lock = xSemaphoreCreateMutex();
	if (self->list_lock == NULL) {
		goto err;
	}

	self->packet_queue = xMessageBufferCreate(queue_len);
	if (self->packet_queue == NULL) {
		goto err;
	}

	self->can_run = true;
	xTaskCreate(collect_task, "mq-lora-c", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->collect_task));
	if (self->collect_task == NULL) {
		goto err;
	}

	xTaskCreate(send_task, "mq-lora-s", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->send_task));
	if (self->send_task == NULL) {
		goto err;
	}

	pbuf_prepare(self);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("service started, max msg size = %ld B, packet size = %ld B, queue len = %ld, "), mq_size, packet_size, queue_len);
	return MQ_LORA_BRIDGE_RET_OK;
err:
	/* Not fully started, stop everything. */
	mq_lora_bridge_stop(self);
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start service"));
	return MQ_LORA_BRIDGE_RET_FAILED;
}


mq_lora_bridge_ret_t mq_lora_bridge_stop(MqLoraBridge *self) {
	self->can_run = false;
	while (self->collect_task_running || self->send_task_running) {
		vTaskDelay(100);
	}
	self->collect_task = NULL;
	self->send_task = NULL;

	if (self->mqc) {
		self->mqc->vmt->close(self->mqc);
	}

	if (self->list_lock) {
		vSemaphoreDelete(self->list_lock);
	}

	if (self->packet_queue) {
		vMessageBufferDelete(self->packet_queue);
	}

	if (self->pbuf) {
		free(self->pbuf);
		self->pbuf_size = 0;
		self->pbuf = NULL;
	}

	if (self->send_pbuf) {
		free(self->send_pbuf);
		self->send_pbuf = NULL;
	}

	if (self->hex_pbuf) {
		free(self->hex_pbuf);
		self->hex_pbuf = NULL;
	}

	ndarray_free(&self->collect_buf);

	/** @todo free topic list */

	return MQ_LORA_BRIDGE_RET_OK;
}


static void lock_list(MqLoraBridge *self) {
	xSemaphoreTake(self->list_lock, portMAX_DELAY);
}


static void unlock_list(MqLoraBridge *self) {
	xSemaphoreGive(self->list_lock);
}


mq_lora_bridge_ret_t mq_lora_bridge_add_topic(
	MqLoraBridge *self,
	const char *topic,
	const char *key,
	uint32_t decimate,
	bool trigger
) {
	if (topic == NULL || key == NULL) {
		return MQ_LORA_BRIDGE_RET_FAILED;
	}

	struct mq_lora_bridge_topic *t = malloc(sizeof(struct mq_lora_bridge_topic));
	if (t == NULL) {
		return MQ_LORA_BRIDGE_RET_FAILED;
	}
	strlcpy(t->topic, topic, MQ_LORA_BRIDGE_MAX_TOPIC_LEN);
	strlcpy(t->key, key, MQ_LORA_BRIDGE_MAX_CBOR_KEY_LEN);
	t->decimate_counter = 0;
	t->decimate = decimate;
	t->this_message_triggers = trigger;

	/** @todo ho-ho-hooo */
	self->mqc->vmt->subscribe(self->mqc, "#");

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("  '%s' -> '%s', decimate %ld, trigger %s"),
		t->topic,
		t->key,
		t->decimate,
		t->this_message_triggers ? "yes" : "no"
	);

	lock_list(self);
	t->next = self->topic_list;
	self->topic_list = t;
	unlock_list(self);

	return MQ_LORA_BRIDGE_RET_OK;
}


