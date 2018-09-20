/*
 * plog-router interface descriptor
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

/**
 * Interface descriptor of a plog-router message queue service
 *
 * Use this interface to create a publish or subscribe client. The interface
 * is host-less, i.e. no host-side interface disconnect or abstraction is
 * required as only a single service implements it (the plog-router service).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "interface.h"


typedef enum {
	IPLOG_RET_OK = 0,
	IPLOG_RET_FAILED,
} iplog_ret_t;


enum iplog_message_type {
	IPLOG_MESSAGE_TYPE_NONE = 0,
	IPLOG_MESSAGE_TYPE_LOG,
	IPLOG_MESSAGE_TYPE_INT32,
	IPLOG_MESSAGE_TYPE_UINT32,
	IPLOG_MESSAGE_TYPE_FLOAT,
	IPLOG_MESSAGE_TYPE_DATA,
};


enum iplog_message_severity {
	IPLOG_MESSAGE_SEVERITY_NONE = 0,
	IPLOG_MESSAGE_SEVERITY_TRACE,
	IPLOG_MESSAGE_SEVERITY_DEBUG,
	IPLOG_MESSAGE_SEVERITY_INFO,
	IPLOG_MESSAGE_SEVERITY_WARNING,
	IPLOG_MESSAGE_SEVERITY_ERROR,
	IPLOG_MESSAGE_SEVERITY_CRITICAL,
};


struct iplog_message_content_data {
	const uint8_t *buf;
	size_t len;
};


struct iplog_message_content_log {
	const char *buf;
	enum iplog_message_severity severity;
};


union iplog_message_content {
	struct iplog_message_content_data data;
	int32_t int32;
	uint32_t uint32;
	float cfloat;
	struct iplog_message_content_log log;
};


typedef struct iplog_message {
	enum iplog_message_type type;
	union iplog_message_content content;
	const char *topic;
	struct timespec time;
} IPlogMessage;


struct plog;

typedef struct iplog {
	Interface interface;

	/* Reception queue of the message router. */
	QueueHandle_t rxqueue;
	QueueHandle_t delivered;

	/* Linked-list of the connected clients. NULL if no client is connected. */
	struct plog *first_client;

} IPlog;


iplog_ret_t iplog_init(IPlog *self);
iplog_ret_t iplog_free(IPlog *self);



