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
#include "relay_msg.pb.h"

#define MODULE_NAME "plog-relay"

/** @todo this doesn't work as expected */
#define INSTANCE_PREFIX(x)  "\x1b[33m" "%s: " "\x1b[0m" x, self->name
#define ASSERT(c, r) if (u_assert(c)) return r


static plog_ret_t plog_relay_plog_recv_handler(void *context, const IPlogMessage *msg) {
	PlogRelay *self = (PlogRelay *)context;

	/* Use the shared message structure. Do not lock it as this function
	 * is called from the main plog-relay task only. */


	self->msg_received = true;

	return PLOG_RET_OK;
}


static void plog_relay_task(void *p) {
	/* Instance passed as a task parameter. */
	PlogRelay *self = (PlogRelay *)p;

	if (u_assert(self->state == PLOG_RELAY_STATE_RUNNING)) {
		goto err;
	}

	/* Allocate/find/open resources. */
	if (self->source_type == PLOG_RELAY_SPEC_TYPE_PLOG) {

		/* Query the first PLOG router in the system. */
		Interface *interface;
		if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_PLOG_ROUTER, 0, &interface) != ISERVICELOCATOR_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("no PLOG router found")));
			goto err;
		}
		self->source.plog.iplog = (IPlog *)interface;

		/* Open the interface and create a client instance. */
		if (plog_open(&self->source.plog.plog, self->source.plog.iplog) != PLOG_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("cannot open the PLOG router interface")));
			goto err;
		}
		plog_set_recv_handler(&self->source.plog.plog, plog_relay_plog_recv_handler, (void *)self);
		if (plog_subscribe(&self->source.plog.plog, self->source.plog.topic) != PLOG_RET_OK) {
			u_log(system_log, LOG_TYPE_ERROR, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("cannot subscribe to '%s'")), self->source.plog.topic);
			goto err;
		} else {
			u_log(system_log, LOG_TYPE_INFO, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("subscribed to '%s'")), self->source.plog.topic);
		}

	} else {
		u_log(system_log, LOG_TYPE_ERROR, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("unknown source specification")));
		goto err;
	}



	while (self->state != PLOG_RELAY_STATE_STOP_REQ) {
		/* Clear the message before doing anything with it. */
		self->msg = (PlogRelayMsg){0};
		self->msg_received = false;

		/* Fill it from the configured source. */
		if (self->source_type == PLOG_RELAY_SPEC_TYPE_PLOG) {
			plog_receive(&self->source.plog.plog, 100);
		}

		if (self->msg_received == false) {
			continue;
		}

		/* Process all rules. */

		/* Send the message to the configured destination. */
		u_log(system_log, LOG_TYPE_DEBUG, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("message received")));
	}

	/* Skipt the task loop on error. */
err:

	/* Release all resources here. */
	if (self->source_type == PLOG_RELAY_SPEC_TYPE_PLOG) {
		plog_close(&self->source.plog.plog);
	}

	self->state = PLOG_RELAY_STATE_STOPPED;
	u_log(system_log, LOG_TYPE_INFO, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("instance stopped")));
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

	u_log(system_log, LOG_TYPE_INFO, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("starting the instance")));

	self->state = PLOG_RELAY_STATE_RUNNING;
	xTaskCreate(plog_relay_task, "plog-relay", configMINIMAL_STACK_SIZE + 512, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		goto err;
	}

	return PLOG_RELAY_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("cannot start the instance")));
	return PLOG_RELAY_RET_FAILED;
}


plog_relay_ret_t plog_relay_stop(PlogRelay *self) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(self->state == PLOG_RELAY_STATE_RUNNING, PLOG_RELAY_RET_BAD_STATE);

	u_log(system_log, LOG_TYPE_INFO, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("stopping the instance")));
	self->state = PLOG_RELAY_STATE_STOP_REQ;
	while (self->state == PLOG_RELAY_STATE_STOP_REQ) {
		vTaskDelay(100);
	}
	ASSERT(self->state == PLOG_RELAY_STATE_STOPPED, PLOG_RELAY_RET_FAILED);
	return PLOG_RELAY_RET_OK;
}


plog_relay_ret_t plog_relay_set_name(PlogRelay *self, const char *name) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(name != NULL, PLOG_RELAY_RET_BAD_ARG);
	ASSERT(self->state == PLOG_RELAY_STATE_STOPPED, PLOG_RELAY_RET_BAD_STATE);

	strlcpy(self->name, name, PLOG_RELAY_INSTANCE_NAME_LEN);

	return PLOG_RELAY_RET_OK;
}


plog_relay_ret_t plog_relay_set_source(PlogRelay *self, union plog_relay_spec *source, enum plog_relay_spec_type type) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(source != NULL, PLOG_RELAY_RET_BAD_ARG);
	ASSERT(self->state == PLOG_RELAY_STATE_STOPPED, PLOG_RELAY_RET_BAD_STATE);

	self->source_type = type;
	self->source = *source;
	u_log(system_log, LOG_TYPE_DEBUG, INSTANCE_PREFIX(U_LOG_MODULE_PREFIX("set source topic=%s")), self->source.plog.topic);

	return PLOG_RELAY_RET_OK;
}


plog_relay_ret_t plog_relay_get_source(PlogRelay *self, union plog_relay_spec *source, enum plog_relay_spec_type *type) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(source != NULL, PLOG_RELAY_RET_BAD_ARG);
	ASSERT(self->state == PLOG_RELAY_STATE_STOPPED, PLOG_RELAY_RET_BAD_STATE);

	*source = self->destination;
	*type = self->source_type;

	return PLOG_RELAY_RET_OK;
}


plog_relay_ret_t plog_relay_set_destination(PlogRelay *self, union plog_relay_spec *destination, enum plog_relay_spec_type type) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(destination != NULL, PLOG_RELAY_RET_BAD_ARG);
	ASSERT(self->state == PLOG_RELAY_STATE_STOPPED, PLOG_RELAY_RET_BAD_STATE);

	self->destination_type = type;
	self->destination = *destination;

	return PLOG_RELAY_RET_OK;
}


plog_relay_ret_t plog_relay_get_destination(PlogRelay *self, union plog_relay_spec *destination, enum plog_relay_spec_type *type) {
	ASSERT(self != NULL, PLOG_RELAY_RET_NULL);
	ASSERT(destination != NULL, PLOG_RELAY_RET_BAD_ARG);
	ASSERT(self->state == PLOG_RELAY_STATE_STOPPED, PLOG_RELAY_RET_BAD_STATE);

	*destination = self->destination;
	*type = self->destination_type;

	return PLOG_RELAY_RET_OK;
}

