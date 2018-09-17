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

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "interface.h"
#include "interfaces/plog/descriptor.h"


iplog_ret_t iplog_init(IPlog *self) {
	if (self == NULL) {
		return IPLOG_RET_FAILED;
	}

	memset(self, 0, sizeof(IPlog));
	uhal_interface_init(&self->interface);

	self->rxqueue = xQueueCreate(1, sizeof(IPlogMessage));
	if(self->rxqueue == NULL) {
		return IPLOG_RET_FAILED;
	}

	/* This queue is used to signal the sender (client) that the message
	 * was successfully processed and delivered to all subscribers.
	 * We will wait here until a single byte is received. This is a
	 * replacement for a semaphore, because FreeRTOS doesn't allow
	 * signaling a semaphore which was not taken beforehand. */
	self->delivered = xQueueCreate(1, sizeof(uint8_t));
	if(self->delivered == NULL) {
		return IPLOG_RET_FAILED;
	}

	return IPLOG_RET_OK;
}


iplog_ret_t iplog_free(IPlog *self) {
	if (self == NULL) {
		return IPLOG_RET_FAILED;
	}

	if (self->rxqueue != NULL) {
		vQueueDelete(self->rxqueue);
	}
	if (self->delivered != NULL) {
		vQueueDelete(self->delivered);
	}

	memset(self, 0, sizeof(IPlog));

	return IPLOG_RET_OK;
}


