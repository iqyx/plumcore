/*
 * CAN interface client
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

#include "main.h"

#include "can.h"
#include "ccan.h"
#include "hcan.h"


ccan_ret_t ccan_open(CCan *self, ICan *ican) {
	if (self == NULL) {
		return CCAN_RET_NULL;
	}
	if (ican == NULL) {
		return CCAN_RET_BAD_ARG;
	}

	memset(self, 0, sizeof(CCan));

	/* Initialize the receiving queue. */
	self->rxqueue = xQueueCreate(CCAN_RX_QUEUE_SIZE, sizeof(ICanMessage));
	if (self->rxqueue == NULL) {
		return CCAN_RET_FAILED;
	}

	/* Reference to the interface descriptor. */
	self->ican = ican;

	/* Add the newly connected client to the client list in the descriptor. */

	xSemaphoreTake(ican->client_list_lock, portMAX_DELAY);
	self->next = ican->first_client;
	ican->first_client = self;
	xSemaphoreGive(ican->client_list_lock);

	return CCAN_RET_OK;
}


ccan_ret_t ccan_add_filter(CCan *self, ICanFilter filter) {
	if (self == NULL) {
		return CCAN_RET_NULL;
	}
	if (self->ican == NULL) {
		return CCAN_RET_NOT_OPENED;
	}
	(void)filter;

	/** @todo not implemented */
	return CCAN_RET_FAILED;
}


ccan_ret_t ccan_receive(CCan *self, uint8_t *buf, size_t *len, uint32_t *id, uint32_t timeout) {
	if (self == NULL) {
		return CCAN_RET_NULL;
	}
	if (self->ican == NULL) {
		return CCAN_RET_NOT_OPENED;
	}

	ICanMessage msg;
	if (xQueueReceive(self->rxqueue, &msg, timeout) != pdTRUE) {
		*len = 0;
		*id = 0;
		return CCAN_RET_EMPTY;
	}
	*len = msg.len;
	*id = msg.id;
	memcpy(buf, &msg.buf, msg.len);

	return CCAN_RET_OK;
}


ccan_ret_t ccan_send(CCan *self, const uint8_t *buf, size_t len, uint32_t id) {
	if (self == NULL) {
		return CCAN_RET_NULL;
	}
	if (self->ican == NULL) {
		return CCAN_RET_NOT_OPENED;
	}

	/* When the client is opened, check if the host is connected to the descriptor. */
	if (self->ican->host == NULL) {
		return CCAN_RET_FAILED;
	}
	if (hcan_send(self->ican->host, buf, len, id) != HCAN_RET_OK) {
		return CCAN_RET_FAILED;
	}

	return CCAN_RET_OK;
}


ccan_ret_t ccan_close(CCan *self) {
	if (self == NULL) {
		return CCAN_RET_NULL;
	}
	if (self->ican == NULL) {
		return CCAN_RET_NOT_OPENED;
	}

	/* Remove this client from the linked list. */
	xSemaphoreTake(self->ican->client_list_lock, portMAX_DELAY);
	CCan *i = self->ican->first_client;
	if (i == self) {
		self->ican->first_client = NULL;
		i = NULL;
	}
	while (i != NULL) {
		if (i->next == self) {
			i->next = self->next;
			break;
		}
		i = i->next;
	}
	xSemaphoreGive(self->ican->client_list_lock);

	/* Clear reference to the interface descriptor. */
	self->ican = NULL;

	/* And free all resources. */
	if (self->rxqueue != NULL) {
		vQueueDelete(self->rxqueue);
	}
	return CCAN_RET_FAILED;
}
