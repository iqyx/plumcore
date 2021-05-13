/* SPDX-License-Identifier: BSD-2-Clause
 *
 * plog message queue router
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include <interfaces/mq.h>
#include <interfaces/clock/descriptor.h>
#include <interfaces/servicelocator.h>

#include "plog_router.h"

#define MODULE_NAME "plog-router"

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


/********************************* MqClient interface ***********************************/



static mq_ret_t plog_router_mq_client_subscribe(MqClient *self, const char *filter) {
	if (u_assert(self != NULL) ||
	    u_assert(filter != NULL)) {
		return MQ_RET_FAILED;
	}
	struct plog_router_mq_client *c = (struct plog_router_mq_client *)self;

	/* We are implementing a single topic filter only. Overwrite the old one. */
	strlcpy(c->topic_filter, filter, PLOG_ROUTER_TOPIC_LEN_MAX);

	return MQ_RET_FAILED;
}


static mq_ret_t plog_router_mq_client_unsubscribe(MqClient *self, const char *filter) {
	if (u_assert(self != NULL) ||
	    u_assert(filter != NULL)) {
		return MQ_RET_FAILED;
	}
	struct plog_router_mq_client *c = (struct plog_router_mq_client *)self;

	c->topic_filter[0] = '\0';

	return MQ_RET_FAILED;
}


static mq_ret_t plog_router_mq_client_receive(MqClient *self, char *topic, size_t topic_size, struct ndarray *array, struct timespec *ts) {
	if (u_assert(self != NULL) ||
	    u_assert(topic != NULL) ||
	    u_assert(array != NULL) ||
	    u_assert(ts != NULL)) {
		return MQ_RET_FAILED;
	}
	struct plog_router_mq_client *c = (struct plog_router_mq_client *)self;

	/* Wait for the message. It may already be delivered, the queue handles that. */
	struct plog_router_msg_send msg_send = {0};
	if (xQueueReceive(c->send_lock, &msg_send, pdMS_TO_TICKS(c->rx_timeout_ms)) == pdTRUE) {
		struct plog_router_msg_recv msg_recv = {0};
	
		strlcpy(topic, msg_send.topic, topic_size);
		memcpy(ts, msg_send.ts, sizeof(struct timespec));

		/* Copy the array metadata, but keep the buffer. */
		array->dtype = msg_send.array->dtype;
		array->dsize = msg_send.array->dsize;
		array->asize = 0;
		ndarray_append(array,msg_send.array);

		/* And pass the response back. */
		xQueueSend(c->recv_lock, &msg_recv, 0);
		return MQ_RET_OK;
	}

	return MQ_RET_TIMEOUT;
}


static mq_ret_t deliver_to_client(struct plog_router_mq_client *to, const char *topic, const struct ndarray *array, const struct timespec *ts) {
	/* Only a single delivery can be made at a time. Lock the msg mutex.
	 * Attempt delivery for a configurable time. */
	if (xSemaphoreTake(to->msg_mutex, portMAX_DELAY) == pdTRUE) {
		/* Let the client know we have a message to deliver. */
		struct plog_router_msg_send msg_send = {
			.topic = topic,
			.array = array,
			.ts = ts
		};
		xQueueSend(to->send_lock, &msg_send, portMAX_DELAY);

		struct plog_router_msg_recv msg_recv = {0};
		if (xQueueReceive(to->recv_lock, &msg_recv, portMAX_DELAY) == pdTRUE) {
			/** @todo handle the return value. */
		}

		/* Message was delivered successfully, let the others in. */
		xSemaphoreGive(to->msg_mutex);
		return MQ_RET_OK;
	}
	return MQ_RET_TIMEOUT;
}


static mq_ret_t plog_router_mq_client_publish(MqClient *self, const char *topic, const struct ndarray *array, const struct timespec *ts) {
	if (u_assert(self != NULL) ||
	    u_assert(topic != NULL) ||
	    u_assert(array != NULL) ||
	    u_assert(ts != NULL)) {
		return MQ_RET_FAILED;
	}
	Mq *mq = self->parent;
	PlogRouter *plog = (PlogRouter *)mq->parent;

	/** @todo */
	struct plog_router_mq_client *c = plog->first_client;
	while (c) {
		if (plog_router_match_topic(c->topic_filter, topic)) {
			deliver_to_client(c, topic, array, ts);
		}
		c = (struct plog_router_mq_client *)c->client.next;
	}

	return MQ_RET_FAILED;
}


static mq_ret_t plog_router_mq_client_close(MqClient *self) {
	if (u_assert(self != NULL)) {
		return MQ_RET_FAILED;
	}

	/** @todo not implemented */
	struct plog_router_mq_client *c = (struct plog_router_mq_client *)self;
	c->topic_filter[0] = '\0';

	return MQ_RET_OK;
}


static mq_ret_t plog_router_mq_client_set_timeout(MqClient *self, uint32_t timeout_ms) {
	if (u_assert(self != NULL)) {
		return MQ_RET_FAILED;
	}

	struct plog_router_mq_client *c = (struct plog_router_mq_client *)self;
	c->rx_timeout_ms = timeout_ms;

	return MQ_RET_OK;
}


static struct mq_client_vmt mq_client_vmt = {
	.subscribe = &plog_router_mq_client_subscribe,
	.unsubscribe = &plog_router_mq_client_unsubscribe,
	.publish = &plog_router_mq_client_publish,
	.receive = &plog_router_mq_client_receive,
	.close = &plog_router_mq_client_close,
	.set_timeout = &plog_router_mq_client_set_timeout,
};


/********************************* Mq interface ***********************************/

static MqClient *plog_router_open(Mq *self) {
	if (u_assert(self != NULL)) {
		return NULL;
	}
	PlogRouter *plog = (PlogRouter *)self->parent;

	/* Allocate some space for a structure holding the client instance. */
	struct plog_router_mq_client *c = malloc(sizeof(struct plog_router_mq_client));
	if (c == NULL) {
		goto err;
	}
	memset(c, 0, sizeof(struct plog_router_mq_client));

	/* Initialize parts needed by the implementing service (plog-router).
	 * The client is not subscribed to anything yet. */
	c->topic_filter[0] = '\0';

	c->msg_mutex = xSemaphoreCreateMutex();
	c->send_lock = xQueueCreate(1, sizeof(struct plog_router_msg_send));
	c->recv_lock = xQueueCreate(1, sizeof(struct plog_router_msg_recv));
	if (c->msg_mutex == NULL || c->send_lock == NULL || c->recv_lock == NULL) {
		goto err;
	}
	c->rx_timeout_ms = PLOG_ROUTER_RX_TIMEOUT_MS_DEFAULT;

	/* And finally initialize the MqClient interface and add the client to the list. */
	mq_client_init(&c->client, self);
	/** @todo add to the list, lock */
	c->client.next = (MqClient *)plog->first_client;
	c->client.vmt = &mq_client_vmt;
	plog->first_client = c;
	/** @todo unlock */
	return &c->client;

err:
	free(c);
	return NULL;
}


static struct mq_vmt mq_vmt = {
	.open = plog_router_open,
};


/********************************* PlogRouter API ***********************************/

plog_router_ret_t plog_router_init(PlogRouter *self) {
	if (u_assert(self != NULL)) {
		return PLOG_ROUTER_RET_NULL;
	}

	memset(self, 0, sizeof(PlogRouter));

	if (mq_init(&self->mq) != MQ_RET_OK) {
		goto ret;
	}
	self->mq.parent = (void *)self;
	self->mq.vmt = &mq_vmt;
	
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("plog message router started"));
	self->initialized = true;
	return PLOG_ROUTER_RET_OK;

ret:
	plog_router_free(self);
	return PLOG_ROUTER_RET_FAILED;
}


plog_router_ret_t plog_router_free(PlogRouter *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->initialized)) {
		return PLOG_ROUTER_RET_NULL;
	}

	mq_free(&self->mq);

	self->initialized = false;
	return PLOG_ROUTER_RET_OK;
}


plog_router_ret_t plog_router_set_clock(PlogRouter *self, Clock *rtc) {
	if (u_assert(self != NULL) ||
	    u_assert(clock != NULL)) {
		return PLOG_ROUTER_RET_NULL;
	}

	self->rtc = rtc;
	
	return PLOG_ROUTER_RET_OK;
}


