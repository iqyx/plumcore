/*
 * CAN interface host
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

#include "can.h"
#include "hcan.h"
#include "ccan.h"
#include "u_log.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "interface-hcan"


hcan_ret_t hcan_init(HCan *self) {
	if (self == NULL) {
		return HCAN_RET_NULL;
	}

	memset(self, 0, sizeof(HCan));

	return HCAN_RET_OK;
}


hcan_ret_t hcan_free(HCan *self) {
	if (self == NULL) {
		return HCAN_RET_NULL;
	}
	if (self->ican != NULL) {
		return HCAN_RET_FAILED;
	}

	return HCAN_RET_OK;
}


hcan_ret_t hcan_connect(HCan *self, ICan *ican) {
	if (self == NULL) {
		return HCAN_RET_NULL;
	}

	if (self->ican != NULL) {
		/* Already connected. */
		return HCAN_RET_FAILED;
	}

	self->ican = ican;
	self->ican->host = self;

	return HCAN_RET_OK;
}


hcan_ret_t hcan_disconnect(HCan *self) {
	if (self == NULL) {
		return HCAN_RET_NULL;
	}

	/* Not connected, cannot disconnect. */
	if (self->ican == NULL) {
		return HCAN_RET_NOT_CONNECTED;
	}

	self->ican->host = NULL;
	self->ican = NULL;

	return HCAN_RET_OK;
}


hcan_ret_t hcan_send(HCan *self, const uint8_t *buf, size_t len, uint32_t id) {
	if (self == NULL) {
		return HCAN_RET_NULL;
	}
	if (self->send == NULL) {
		return HCAN_RET_FAILED;
	}
	return self->send(self->send_context, buf, len, id);
}


hcan_ret_t hcan_set_send_callback(HCan *self, hcan_ret_t (*send)(void *context, const uint8_t *buf, size_t len, uint32_t id), void *send_context) {
	if (self == NULL) {
		return HCAN_RET_NULL;
	}
	self->send = send;
	self->send_context = send_context;

	return HCAN_RET_OK;
}


hcan_ret_t hcan_received(HCan *self, const uint8_t *buf, size_t len, uint32_t id) {
	if (self == NULL) {
		return HCAN_RET_NULL;
	}
	if (buf == NULL || len == 0) {
		return HCAN_RET_BAD_ARG;
	}
	if (self->ican == NULL) {
		return HCAN_RET_NOT_CONNECTED;
	}

	CCan *i = self->ican->first_client;
	while (i != NULL) {
		ICanMessage msg = {0};
		msg.id = id;
		msg.len = len;
		memcpy(&msg.buf, buf, len);

		if (xQueueSendToBack(i->rxqueue, &msg, 0) != pdTRUE) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("rx queue full"));
		}

		i = i->next;
	}

	return HCAN_RET_OK;
}
