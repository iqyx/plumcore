/*
 * plog message packager/serializer
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "module.h"

#include "interfaces/plog/descriptor.h"
#include "interfaces/plog/client.h"
#include "heatshrink_encoder.h"


#define PLOG_PACKAGER_NONCE_SIZE 16
#define PLOG_PACKAGER_MAX_KEY_SIZE 32
#define PLOG_PACKAGER_TOPIC_FILTER_SIZE 32
#define PLOG_PACKAGER_DATA_SIZE 256
#define PLOG_PACKAGER_DATA_SIZE_THR 200
#define PLOG_PACKAGER_HEADER_SIZE 64

#define PLOG_PACKAGER_PACKAGE_HEADER (uint8_t[]){0xf2, 0xd8, 0xe4, 0x04}
#define PLOG_PACKAGER_PACKAGE_TRAILER (uint8_t[]){0x29, 0x88, 0x5b, 0xd5}
#define PLOG_PACKAGER_NONCE_HEADER_16 (uint8_t[]){0x9e, 0x41, 0xb0, 0xd6}
#define PLOG_PACKAGER_MLIST_HEADER1 (uint8_t[]){0xbf, 0xed, 0x30, 0xfc}
#define PLOG_PACKAGER_INDEX_HEADER (uint8_t[]){0xe8, 0x35, 0xbe, 0x44}
#define PLOG_PACKAGER_MAC_POLY1305_HEADER (uint8_t[]){0x50, 0xd9, 0x8b, 0x7e}

#define PLOG_PACKAGER_M_NONE_HEADER (uint8_t[]){0x51, 0xe2}
#define PLOG_PACKAGER_M_FLOAT_HEADER (uint8_t[]){0x9e, 0xca}


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
	uint8_t *data;
	size_t header_size;
	size_t header_used;

	uint16_t index;
	uint32_t message_count;
	volatile bool finished;
};


typedef struct plog_packager {
	Module module;

	bool initialized;
	bool debug;

	TaskHandle_t task;
	volatile bool can_run;
	volatile bool running;

	TaskHandle_t sender_task;
	volatile bool sender_can_run;
	volatile bool sender_running;

	Plog plog;
	char topic_filter[PLOG_PACKAGER_TOPIC_FILTER_SIZE];
	char dest_topic[PLOG_PACKAGER_TOPIC_FILTER_SIZE];
	uint8_t key[PLOG_PACKAGER_MAX_KEY_SIZE];
	size_t key_size;

	uint8_t nonce[PLOG_PACKAGER_NONCE_SIZE];
	uint32_t package_counter;
	struct plog_packager_package package;

	heatshrink_encoder *hs_encoder;

} PlogPackager;


plog_packager_ret_t plog_packager_init(PlogPackager *self, const char *topic_filter, const char *dest_topic);
plog_packager_ret_t plog_packager_free(PlogPackager *self);


