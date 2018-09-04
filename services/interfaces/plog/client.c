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

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "u_assert.h"
#include "u_log.h"

#include "interfaces/plog/descriptor.h"
#include "interfaces/plog/client.h"


plog_ret_t plog_open(Plog *self, IPlog *iplog) {
	if (self == NULL) {
		return PLOG_RET_NULL;
	}
	if (iplog == NULL) {
		return PLOG_RET_BAD_ARG;
	}

	memset(self, 0, sizeof(Plog));

	/* Add the newly connected client to the client list in the descriptor. */
	/** @todo lock the list first */
	self->next = iplog->first_client;
	iplog->first_client = self;

	self->txqueue = xQueueCreate(1, sizeof(struct IPlogMessage *));
	if(self->txqueue == NULL) {
		return PLOG_RET_FAILED;
	}
	self->processed = xQueueCreate(1, sizeof(uint8_t));
	if(self->processed == NULL) {
		return PLOG_RET_FAILED;
	}

	/* Reference to the interface descriptor. It is set at the very end to
	 * indicate that the instance is properly connected to the descriptor.*/
	self->iplog = iplog;

	return PLOG_RET_OK;
}


plog_ret_t plog_close(Plog *self) {
	if (self == NULL) {
		return PLOG_RET_NULL;
	}
	if (self->iplog == NULL) {
		return PLOG_RET_NOT_OPENED;
	}

	/* Remove this client from the linked list. */
	Plog *i = self->iplog->first_client;
	if (i == self) {
		self->iplog->first_client = NULL;
		i = NULL;
	}
	while (i != NULL) {
		if (i->next == self) {
			i->next = self->next;
			break;
		}
		i = i->next;
	}

	if (self->txqueue != NULL) {
		vQueueDelete(self->txqueue);
	}
	if (self->processed != NULL) {
		vQueueDelete(self->processed);
	}

	/* Clear reference to the interface descriptor. The instance is no longer
	 * correctly connected to the descriptor. */
	self->iplog = NULL;

	return PLOG_RET_FAILED;
}


plog_ret_t plog_publish(Plog *self, const IPlogMessage *msg) {
	if (self == NULL) {
		return PLOG_RET_NULL;
	}
	if (self->iplog == NULL) {
		return PLOG_RET_NOT_OPENED;
	}

	/** @todo lock the queue first */

	/* Send the message as a reference. Wait until the plog-router
	 * is ready to accept the message (wait for the queue to become empty).
	 * Wait indefinitely otherwise. */
	if (xQueueSend(self->iplog->rxqueue, &msg, portMAX_DELAY) != pdTRUE) {
		return PLOG_RET_FAILED;
	}

	/* When the message was enqueued, the plog-router has some time to process
	 * and deliver it. Wait until it sends us a notification of delivery.
	 * If the notification is not received, fail. */
	uint8_t tmp = 0;
	if (xQueueReceive(self->iplog->delivered, &tmp, portMAX_DELAY) != pdTRUE) {
		return PLOG_RET_FAILED;
	}
	(void)tmp;
	/* Message was delivered successfully, we can release the *msg (goes out of scope). */

	return PLOG_RET_OK;
}


/* Helper functions for publishing different kinds of data. */

plog_ret_t plog_publish_data(Plog *self, const char *topic, const uint8_t *buf, size_t len) {
	if (self == NULL) {
		return PLOG_RET_NULL;
	}
	if (self->iplog == NULL) {
		return PLOG_RET_NOT_OPENED;
	}

	IPlogMessage m;
	memset(&m, 0, sizeof(IPlogMessage));
	m.type = IPLOG_MESSAGE_TYPE_DATA;
	m.topic = topic;
	m.content.data.buf = buf;
	m.content.data.len = len;

	return plog_publish(self, &m);
}


plog_ret_t plog_publish_log(Plog *self, enum iplog_message_severity severity, const char *topic, const char *msg) {
	if (self == NULL) {
		return PLOG_RET_NULL;
	}
	if (self->iplog == NULL) {
		return PLOG_RET_NOT_OPENED;
	}

	IPlogMessage m;
	memset(&m, 0, sizeof(IPlogMessage));
	m.type = IPLOG_MESSAGE_TYPE_LOG;
	m.topic = topic;
	m.content.log.buf = msg;
	m.content.log.severity = severity;

	return plog_publish(self, &m);
}


plog_ret_t plog_publish_printf(Plog *self, enum iplog_message_severity severity,
                               const char *topic, const char *msg, ...) {


}


plog_ret_t plog_publish_int32(Plog *self, const char *topic, const int32_t v) {


}


plog_ret_t plog_publish_uint32(Plog *self, const char *topic, const uint32_t v) {


}


plog_ret_t plog_publish_float(Plog *self, const char *topic, const float v) {
	if (self == NULL) {
		return PLOG_RET_NULL;
	}
	if (self->iplog == NULL) {
		return PLOG_RET_NOT_OPENED;
	}

	IPlogMessage m;
	memset(&m, 0, sizeof(IPlogMessage));
	m.type = IPLOG_MESSAGE_TYPE_FLOAT;
	m.topic = topic;
	m.content.cfloat = v;

	return plog_publish(self, &m);
}


plog_ret_t plog_subscribe(Plog *self, const char *topic) {
	if (self == NULL) {
		return PLOG_RET_NULL;
	}
	if (self->iplog == NULL) {
		return PLOG_RET_NOT_OPENED;
	}

	/* Set the topic filter in the connected client instance. It is checked every time
	 * a new message delivery is requested. */
	self->topic_filter = topic;

	return PLOG_RET_OK;
}


plog_ret_t plog_receive(Plog *self, uint32_t timeout) {
	if (self == NULL) {
		return PLOG_RET_NULL;
	}
	if (self->iplog == NULL) {
		return PLOG_RET_NOT_OPENED;
	}

	/* Wait for a message to arrive. */
	IPlogMessage *msg = NULL;
	if (xQueueReceive(self->txqueue, (void *)&msg, timeout) != pdTRUE) {
		return PLOG_RET_EMPTY;
	}

	/* If the message is received properly, check if there is a reception
	 * handler set. If yes, try to call it and examine its return value. */
	if (self->recv_handler != NULL) {
		if (self->recv_handler(self->recv_handler_context, msg) != PLOG_RET_OK) {
			return PLOG_RET_FAILED;
		}
	}

	/* Regardless of the recv_handler return value, confirm the delivery to the
	 * message router. */
	/** @todo pass the result to the router */

	uint8_t tmp = 1;
	if (xQueueSend(self->processed, &tmp, portMAX_DELAY) != pdTRUE) {
		return PLOG_RET_FAILED;
	}

	return PLOG_RET_OK;
}


plog_ret_t plog_set_recv_handler(Plog *self, plog_ret_t (*recv_handler)(void *context, const IPlogMessage *msg),
                                 void *recv_handler_context) {
	if (self == NULL) {
		return PLOG_RET_NULL;
	}
	if (self->iplog == NULL) {
		return PLOG_RET_NOT_OPENED;
	}

	if (recv_handler == NULL) {
		return PLOG_RET_BAD_ARG;
	}

	self->recv_handler = recv_handler;
	self->recv_handler_context = recv_handler_context;

	return PLOG_RET_OK;
}
