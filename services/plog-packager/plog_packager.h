/* SPDX-License-Identifier: BSD-2-Clause
 *
 * plog message packager/serializer
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <interfaces/mq.h>
#include <types/ndarray.h>
#include "heatshrink_encoder.h"


#define PLOG_PACKAGER_NONCE_SIZE 16
#define PLOG_PACKAGER_MAX_KEY_SIZE 32
#define PLOG_PACKAGER_TOPIC_FILTER_SIZE 32
#define PLOG_PACKAGER_HEADER_SIZE 64

/* Randomly generated header allows us to find the package in arbitrary data. */
#define PLOG_PACKAGER_PACKAGE_MAGIC ((uint8_t[]){'P', 'K', 'G'})
#define PLOG_PACKAGER_PACKAGE_MAGIC_SIZE 3

typedef enum {
	PLOG_PACKAGER_RET_OK = 0,
	PLOG_PACKAGER_RET_FAILED,
	PLOG_PACKAGER_RET_NULL,
	PLOG_PACKAGER_RET_BAD_ARG,
} plog_packager_ret_t;


struct plog_packager;
struct plog_packager_package {

	struct plog_packager *parent;

	/* Buffer for the serialized package. *data is a pointer to the MIDDLE of the
	 * allocated buffer. It spans header_size bytes in front of the *data pointer
	 * and data_size after the *data pointer. */
	size_t data_size;
	size_t data_used;
	size_t data_used_raw;
	uint8_t *data;
	size_t header_size;
	size_t header_used;

	uint16_t index;
	uint32_t message_count;
	volatile bool finished;
};


typedef struct plog_packager {
	bool debug;

	TaskHandle_t task;
	volatile bool can_run;
	volatile bool running;

	MqClient *mqc;
	Mq *mq;
	char topic_filter[PLOG_PACKAGER_TOPIC_FILTER_SIZE];
	char dst_topic[PLOG_PACKAGER_TOPIC_FILTER_SIZE];
	uint8_t key[PLOG_PACKAGER_MAX_KEY_SIZE];
	size_t key_size;

	uint8_t nonce[PLOG_PACKAGER_NONCE_SIZE];
	size_t nonce_size;
	uint32_t package_counter;
	struct plog_packager_package package;

	heatshrink_encoder *hs_encoder;
	uint32_t hs_window_size;
	uint32_t hs_lookahead_size;
	
	NdArray rxbuf;

} PlogPackager;


plog_packager_ret_t plog_packager_init(PlogPackager *self, Mq *mq);
plog_packager_ret_t plog_packager_free(PlogPackager *self);
plog_packager_ret_t plog_packager_start(PlogPackager *self, size_t msg_size, size_t package_size);
plog_packager_ret_t plog_packager_stop(PlogPackager *self);
plog_packager_ret_t plog_packager_add_filter(PlogPackager *self, const char *topic_filter, const char *dst_topic);
plog_packager_ret_t plog_packager_set_nonce(PlogPackager *self, const uint8_t *nonce, size_t len);
plog_packager_ret_t plog_packager_set_key(PlogPackager *self, const uint8_t *key, size_t len);


