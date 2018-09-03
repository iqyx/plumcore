/*
 * plog message queue router
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "interfaces/plog/descriptor.h"
#include "interfaces/plog/client.h"

#include "plog_router.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "plog-router"


/**
 * @todo doc
 * @todo coding style
 * @todo debug logging

/* MQTT style topic matching.
 *
 * sport/tennis/player1/#, sport/tennis/player1 -> true
 * sport/tennis/player1/#, sport/tennis/player1/ranking -> true
 * sport/tennis/player1/#, sport/tennis/player1/score/wimbledon -> true
 * sport/#, sport -> true
 * #, anything -> true
 * sport/tennis#, anything -> false
 * sport/tennis/#/ranking, anything -> false
 * sport/tennis/+, sport/tennis/player1 -> true
 * sport/tennis/+, sport/tennis/player2 -> true
 * sport/tennis/+, sport/tennis/player1/ranking -> false
 */
static bool plog_router_match_topic(const char *filter, const char *topic) {
	if (*filter == '#') {
		return true;
	}

	char last_filter = '\0';
	while (true) {
		printf("%c == %c\n", *filter, *topic);
		if (*filter == '#' && *topic != '\0') {
			topic++;
			continue;
		}
		if (*filter == '+' && *topic != '\0' && *topic != '/') {
			topic++;
			continue;
		}
		if (*filter == '+' && *topic == '/') {
			last_filter = *filter;
			filter++;
			continue;
		}
		if (*filter == '#' && last_filter != '/') {
			break;
		}
		if (*filter == '+' && last_filter != '/') {
			break;
		}
		if ((*filter == '/' || *filter == '#' || *filter == '+') && *topic == '\0') {
			last_filter = *filter;
			filter++;
			continue;
		}
		if (*filter == '\0' && *topic == '\0') {
			return true;
		}
		if (*filter == *topic) {
			topic++;
			last_filter = *filter;
			filter++;
			continue;
		}
		break;
	}
	return false;
}


static plog_router_ret_t plog_router_process(PlogRouter *self, IPlogMessage *msg) {

	/* Refuse to resend NULL messages. */
	if (msg == NULL) {
		return PLOG_RET_BAD_ARG;
	}

	/** @todo lock while traversing the list! */
	Plog *p = self->iplog.first_client;
	while (p != NULL) {
		if (plog_router_match_topic(p->topic_filter, msg->topic)) {
			u_log(
				system_log,
				LOG_TYPE_DEBUG,
				U_LOG_MODULE_PREFIX("message '%s' delivered to a client, filter = %s"),
				msg->topic,
				p->topic_filter
			);

			if (xQueueSend(p->txqueue, &msg, portMAX_DELAY) != pdTRUE) {
				return PLOG_RET_FAILED;
			}

			/* Wait for the message to be processed. */
			uint8_t tmp = 0;
			if (xQueueReceive(p->processed, &tmp, 100) != pdTRUE) {
				u_log(
					system_log,
					LOG_TYPE_DEBUG,
					U_LOG_MODULE_PREFIX("message '%s' was not processed in time"),
					msg->topic
				);
				return PLOG_RET_FAILED;
			}
			(void)tmp;
		}
		p = p->next;
	}
	return PLOG_ROUTER_RET_OK;
}


static void plog_router_task(void *p) {
	PlogRouter *self = (PlogRouter *)p;

	self->running = true;
	while (self->can_run) {

		IPlogMessage *msg = NULL;

		/* The rxqueue is a single element long. Peek the message first without removing it
		 * to prevent another client submitting a message until the current one is processed.
		 * It is important because only a single queue is used to signal message delivery. */
		if (xQueuePeek(self->iplog.rxqueue, (void *)&msg, portMAX_DELAY) != pdTRUE) {
			/* Something went wrong. Repeating the step is the most reasonable
			 * thing we can do now. */
			continue;
		}

		if (plog_router_process(self, msg) != PLOG_ROUTER_RET_OK) {
			/* Message cannot be processed. Ignore it and send delivery notification
			 * to avoid blocking the sender. */
			/** @todo find a way to pass a proper return code back to the sender */
		}

		/* Signal delivery first. Do not remove the message from the queue yet. */
		/* Arbitrary number. */
		uint8_t tmp = 1;
		if (xQueueSend(self->iplog.delivered, &tmp, portMAX_DELAY) != pdTRUE) {
			/* If delivery cannot be signaled, simply ignore it. The client
			 * will time out waiting for it. */
			continue;
		}

		/* Now it is the time to allow some other process to submit its message. */
		xQueueReceive(self->iplog.rxqueue, (void *)msg, 0);

	}
	vTaskDelete(NULL);
	self->running = false;
}


plog_router_ret_t plog_router_init(PlogRouter *self) {
	if (u_assert(self != NULL)) {
		return PLOG_ROUTER_RET_NULL;
	}

	memset(self, 0, sizeof(PlogRouter));

	/* Initialize the PLog interface for clients. */
	if (iplog_init(&self->iplog) != IPLOG_RET_OK) {
		return PLOG_ROUTER_RET_FAILED;
	}

	/* Create the router main task. */
	self->can_run = true;
	xTaskCreate(plog_router_task, "plog-router", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return PLOG_ROUTER_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("plog message router started"));
	self->initialized = true;
	return PLOG_ROUTER_RET_OK;
}


plog_router_ret_t plog_router_free(PlogRouter *self) {
	if (self == NULL) {
		return PLOG_ROUTER_RET_NULL;
	}

	if (self->initialized == false) {
		return PLOG_ROUTER_RET_FAILED;
	}

	self->can_run = false;
	while (self->running) {
		vTaskDelay(10);
	}

	/** @todo cannot free the interface descriptor if there are any clients connected */
	iplog_free(&self->iplog);

	self->initialized = false;
	return PLOG_ROUTER_RET_OK;
}




