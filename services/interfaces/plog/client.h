/*
 * plog-router interface client (publish and subscribe)
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
 * Plog client interface used to connect to the IPlog interface descriptor
 * of the plog-router service.
 * This is a "client-disconnectable" interface type with client decoupling
 * (see the documentation). The client uses a message queue IPC to communicate
 * with the service.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "interfaces/plog/descriptor.h"


/* Using plog prefix instead of cplog for clarity as no conflict is expected. */
typedef enum {
	PLOG_RET_OK = 0,
	PLOG_RET_FAILED,
	PLOG_RET_NULL,
	PLOG_RET_BAD_ARG,
	PLOG_RET_NOT_OPENED,
	PLOG_RET_EMPTY,
} plog_ret_t;


typedef struct plog {
	/* Reference to the interface descriptor. Not null if correctly opened. */
	IPlog *iplog;

	/* Transmit queue of the message router (one for each client). */
	/** @todo rename the queues to be more clear */
	QueueHandle_t txqueue;
	QueueHandle_t processed;

	const char *topic_filter;
	plog_ret_t (*recv_handler)(void *context, const IPlogMessage *msg);
	void *recv_handler_context;

	struct plog *next;
} Plog;


/**
 * @brief Create a new client instance
 *
 * Open the @p iplog interface descriptor, make an Plog client interface
 * instance and connect the two together.
 *
 * @param self Uninitialized space of the Plog type
 * @param iplog Interface descriptor to connect to
 *
 * @return PLOG_RET_OK when sucessfully initialized and connected,
 *         PLOG_RET_NULL if a required instance pointer is NULL,
 *         PLOG_RET_BAD_ARG if any other argument is invalid or
 *         PLOG_RET_FAILED on other errors.
 */
plog_ret_t plog_open(Plog *self, IPlog *iplog);
plog_ret_t plog_close(Plog *self);

plog_ret_t plog_publish(Plog *self, const IPlogMessage *msg);

plog_ret_t plog_publish_data(Plog *self, const char *topic, const uint8_t *buf, size_t len);

plog_ret_t plog_publish_log(Plog *self, enum iplog_message_severity severity, const char *topic,
                            const char *msg);

plog_ret_t plog_publish_printf(Plog *self, enum iplog_message_severity severity, const char *topic,
                               const char *msg, ...);

#define plog_printf plog_publish_printf

plog_ret_t plog_publish_int32(Plog *self, const char *topic, const int32_t v);

plog_ret_t plog_publish_uint32(Plog *self, const char *topic, const uint32_t v);

plog_ret_t plog_publish_float(Plog *self, const char *topic, const float v);

plog_ret_t plog_subscribe(Plog *self, const char *topic);

plog_ret_t plog_receive(Plog *self);

plog_ret_t plog_set_recv_handler(Plog *self, plog_ret_t (*recv_handler)(void *context, const IPlogMessage *msg),
                                 void *recv_handler_context);



