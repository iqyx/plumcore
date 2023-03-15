/* SPDX-License-Identifier: BSD-2-Clause
 *
 * MQ -> LoRaWAN bridge
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <message_buffer.h>

#include <interfaces/mq.h>
#include <types/ndarray.h>
#include <interfaces/lora.h>

#include <cbor.h>

#define MQ_LORA_BRIDGE_MAX_TOPIC_LEN 32
#define MQ_LORA_BRIDGE_MAX_CBOR_KEY_LEN 8

typedef enum {
	MQ_LORA_BRIDGE_RET_OK = 0,
	MQ_LORA_BRIDGE_RET_FAILED,
} mq_lora_bridge_ret_t;


struct mq_lora_bridge_topic {
	/* This is a single topic we are converting to a key. It is not a MQTT-formatted filter. */
	char topic[MQ_LORA_BRIDGE_MAX_TOPIC_LEN];
	/* A key within the CBOR map to output the value as. */
	char key[MQ_LORA_BRIDGE_MAX_CBOR_KEY_LEN];

	/**
	 * Output only the n-th message. Count each message in @p decimate_counter and use the value
	 * from the message if it reaches @p decimate. Reset @p decimate_counter back to 0.
	 */
	uint32_t decimate_counter;
	uint32_t decimate;

	/**
	 * If a message corresponding to this @p topic is received, trigger serialising and sending
	 * packet immediately regardless of the current packet buffer size.
	 */
	bool this_message_triggers;

	/* All those configuration structures form a linked list when added. */
	struct mq_lora_bridge_topic *next;
};

typedef struct {
	Mq *mq;
	MqClient *mqc;
	LoRa *lora;

	volatile bool can_run;

	TaskHandle_t collect_task;
	volatile bool collect_task_running;
	NdArray collect_buf;

	TaskHandle_t send_task;
	volatile bool send_task_running;
	uint8_t *send_pbuf;

	/* Hex buffer is double the size of pbuf and send_pbuf */
	char *hex_pbuf;

	MessageBufferHandle_t packet_queue;

	/**
	 * Scratchpad packet buffer. Messages are being serialised and put inside until
	 * a trigger is received. The current packet size is in @p packet_len.
	 */
	uint8_t *pbuf;
	size_t pbuf_size;

	CborEncoder encoder;
	CborEncoder encoder_map;

	/**
	 * Minimum size of the packet needed in order to be sent over the LoRaWAN interface.
	 */
	size_t trigger_size;

	SemaphoreHandle_t list_lock;
	struct mq_lora_bridge_topic *topic_list;
} MqLoraBridge;


mq_lora_bridge_ret_t mq_lora_bridge_init(MqLoraBridge *self, Mq *mq, LoRa *lora);
mq_lora_bridge_ret_t mq_lora_bridge_free(MqLoraBridge *self);
mq_lora_bridge_ret_t mq_lora_bridge_start(MqLoraBridge *self, size_t mq_size, size_t packet_size, size_t queue_len);
mq_lora_bridge_ret_t mq_lora_bridge_stop(MqLoraBridge *self);

mq_lora_bridge_ret_t mq_lora_bridge_add_topic(
	MqLoraBridge *self,
	const char *topic,
	const char *key,
	uint32_t decimate,
	bool trigger
);
