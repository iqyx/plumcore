/*
 * plog message relay
 *
 * Copyright (c) 2019, Marek Koza (qyx@krtko.org)
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "main.h"

#include "interface.h"
#include "interfaces/plog/descriptor.h"
#include "interfaces/plog/client.h"

#include "plog-relay.h"

#define MODULE_NAME "plog-relay"
#define ASSERT(c, r) if (u_assert(c)) return r


static void plog_relay_task(void *p) {
	/* Instance passed as a task parameter. */
	PlogRelay *self = (PlogRelay *)p;

	if (u_assert(self->state == PLOG_RELAY_STATE_RUNNING)) {
		goto err;
	}

	while (self->state != PLOG_RELAY_STATE_STOP_REQ) {
		// plog_receive(&self->plog, 100);

		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("task"));
		vTaskDelay(2000);
	}
err:
	self->state = PLOG_RELAY_STATE_STOPPED;
	vTaskDelete(NULL);
}


plog_relay_ret_t plog_relay_init(PlogRelay *self) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);

	memset(self, 0, sizeof(PlogRelay));

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	self->state = PLOG_RELAY_STATE_STOPPED;
	return PLOG_RELAY_RET_OK;
}


plog_relay_ret_t plog_relay_free(PlogRelay *self) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(self->state == PLOG_RELAY_STATE_STOPPED, PLOG_RELAY_RET_BAD_STATE);

	/* Nothing to free. */
	self->state = PLOG_RELAY_STATE_UNINITIALIZED;

	return PLOG_RELAY_RET_OK;
}


plog_relay_ret_t plog_relay_start(PlogRelay *self) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(self->state == PLOG_RELAY_STATE_STOPPED, PLOG_RELAY_RET_BAD_STATE);

	self->state = PLOG_RELAY_STATE_RUNNING;
	xTaskCreate(plog_relay_task, "plog-relay", configMINIMAL_STACK_SIZE + 512, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s: instance started"), self->name);
	return PLOG_RELAY_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%s: cannot start the instance"), self->name);
	return PLOG_RELAY_RET_FAILED;
}


plog_relay_ret_t plog_relay_stop(PlogRelay *self) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(self->state == PLOG_RELAY_STATE_RUNNING, PLOG_RELAY_RET_BAD_STATE);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s: stopping"), self->name);
	self->state = PLOG_RELAY_STATE_STOP_REQ;
	while (self->state == PLOG_RELAY_STATE_STOP_REQ) {
		vTaskDelay(100);
	}
	ASSERT(self->state == PLOG_RELAY_STATE_STOPPED, PLOG_RELAY_RET_FAILED);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s: instance stopped"), self->name);
	return PLOG_RELAY_RET_OK;
}


plog_relay_ret_t plog_relay_set_name(PlogRelay *self, const char *name) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(name != NULL, PLOG_RELAY_RET_BAD_ARG);

	strlcpy(self->name, name, PLOG_RELAY_INSTANCE_NAME_LEN);

	return PLOG_RELAY_RET_OK;
}
