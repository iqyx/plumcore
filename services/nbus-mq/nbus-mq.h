/* SPDX-License-Identifier: BSD-2-Clause
 *
 * NBUS message queue bridge service
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <services/nbus/nbus.h>
#include <interfaces/mq.h>
#include <types/ndarray.h>


/* Maximum length of the CBOR array containing serialised messages (in bytes). */
#define NBUS_MQ_MSG_BUFFER_SIZE 128
#define NBUS_MQ_MSG_BUFFERS 4
#define NBUS_MQ_MAX_TOPIC_LEN 32
#define NBUS_MQ_NBUS_BUF_LEN 256


typedef enum {
	NBUS_MQ_RET_OK = 0,
	NBUS_MQ_RET_FAILED,
} nbus_mq_ret_t;

enum nbus_mq_msg_buffer_state {
	NBUS_MQ_MB_STATE_EMPTY = 0,
	NBUS_MQ_MB_STATE_ACTIVE,
	NBUS_MQ_MB_STATE_FULL,
};

struct nbus_mq_msg_buffer {
	enum nbus_mq_msg_buffer_state state;
	uint8_t data[NBUS_MQ_MSG_BUFFER_SIZE];
	size_t len;
	time_t time_base;
};


typedef struct nbus_mq {
	NbusChannel channel;
	TaskHandle_t nbus_task;
	uint8_t nbus_buf[NBUS_MQ_NBUS_BUF_LEN];


	/* Input message buffers. */
	struct nbus_mq_msg_buffer msg_buffers[NBUS_MQ_MSG_BUFFERS];

	/* Message queue reception */
	Mq *mq;
	MqClient *mqc;
	volatile bool rx_can_run;
	volatile bool rx_running;
	NdArray rx_buf;
	TaskHandle_t rx_task;

} NbusMq;


nbus_mq_ret_t nbus_mq_init(NbusMq *self, Mq *mq, NbusChannel *parent, const char *name);
nbus_mq_ret_t nbus_mq_free(NbusMq *self);
nbus_mq_ret_t nbus_mq_start(NbusMq *self, const char *topic);
nbus_mq_ret_t nbus_mq_stop(NbusMq *self);
