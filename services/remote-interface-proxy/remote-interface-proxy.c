/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Proxy exporting legacy Datagram protocol handlers as a RemoteInterface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <main.h>
#include <message_buffer.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/datagram.h>
#include <interfaces/remote-interface.h>
#include <interfaces/notify.h>

#include "remote-interface-proxy.h"

#define MODULE_NAME "remote-interface-proxy"


/**********************************************************************************************************************
 * Datagram interface API (protocol handler side)
 **********************************************************************************************************************/

static datagram_ret_t dgram_write(Datagram *self, const void *buf, size_t len, const struct datagram_msg *msg) {
	(void)msg;
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL)) {
		return DATAGRAM_RET_BAD_ARG;
	}
	struct remote_interface_proxy_session *s = self->parent;

	if (len == 0 || len > s->parent->conf.desc.mtu) {
		return DATAGRAM_RET_BAD_ARG;
	}

	/* Never block, the handler would stall if the transport stops reading. The message is dropped if the slot
	 * is free or the buffer is full. The pending count is incremented before sending so a concurrent transport
	 * read never decrements it below zero. */
	datagram_ret_t ret = DATAGRAM_RET_FAILED;
	xSemaphoreTake(s->parent->lock, portMAX_DELAY);
	if (s->used) {
		s->tx_pending++;
		if (xMessageBufferSend(s->tx, buf, len, 0) == len) {
			if (s->notify != NULL) {
				s->notify->vmt->notify(s->notify, s->tx_pending);
			}
			ret = DATAGRAM_RET_OK;
		} else {
			s->tx_pending--;
		}
	}
	xSemaphoreGive(s->parent->lock);

	return ret;
}


static datagram_ret_t dgram_read(Datagram *self, void *buf, size_t *len, struct datagram_msg *msg) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len != NULL)) {
		return DATAGRAM_RET_BAD_ARG;
	}
	struct remote_interface_proxy_session *s = self->parent;

	if (msg != NULL) {
		memset(msg, 0, sizeof(struct datagram_msg));
	}

	/* Block until the transport writes a message. The buffer must be at least desc.mtu bytes long, otherwise
	 * the message stays queued and 0 is returned. */
	size_t got = xMessageBufferReceive(s->rx, buf, *len, portMAX_DELAY);
	if (got == 0) {
		return DATAGRAM_RET_FAILED;
	}
	*len = got;
	return DATAGRAM_RET_OK;
}


static const struct datagram_vmt remote_interface_proxy_datagram_vmt = {
	.write = dgram_write,
	.read = dgram_read,
};


/**********************************************************************************************************************
 * RemoteInterface API (transport side)
 **********************************************************************************************************************/

static bool session_valid(RemoteInterfaceProxy *self, struct remote_interface_proxy_session *s) {
	return s >= self->sessions && s < self->sessions + self->conf.desc.max_sessions && s->used;
}


static remote_interface_ret_t ri_open(RemoteInterface *iface, Notify *notify, void **session, uint32_t timeout_ms) {
	if (u_assert(iface != NULL) ||
	    u_assert(session != NULL)) {
		return REMOTE_INTERFACE_RET_NULL;
	}
	RemoteInterfaceProxy *self = iface->parent;

	if (xSemaphoreTake(self->free_slots, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
		return REMOTE_INTERFACE_RET_TIMEOUT;
	}

	/* A free slot is guaranteed by the counting semaphore. */
	xSemaphoreTake(self->lock, portMAX_DELAY);
	for (uint8_t i = 0; i < self->conf.desc.max_sessions; i++) {
		if (!self->sessions[i].used) {
			self->sessions[i].used = true;
			self->sessions[i].notify = notify;
			self->sessions[i].tx_pending = 0;
			*session = &self->sessions[i];
			break;
		}
	}
	xSemaphoreGive(self->lock);

	return REMOTE_INTERFACE_RET_OK;
}


static remote_interface_ret_t ri_close(RemoteInterface *iface, void *session) {
	if (u_assert(iface != NULL) ||
	    u_assert(session != NULL)) {
		return REMOTE_INTERFACE_RET_NULL;
	}
	RemoteInterfaceProxy *self = iface->parent;
	struct remote_interface_proxy_session *s = session;

	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (!session_valid(self, s)) {
		xSemaphoreGive(self->lock);
		return REMOTE_INTERFACE_RET_BAD_ARG;
	}

	/* Drop all pending messages. A reset fails only if a task is blocked on the buffer: the handler waiting for
	 * a message in an empty rx buffer. Nothing ever blocks on tx while the slot is used (the handler never waits,
	 * the transport does not read concurrently with close). The result can be ignored. */
	xMessageBufferReset(s->rx);
	xMessageBufferReset(s->tx);
	s->used = false;
	s->notify = NULL;
	s->tx_pending = 0;
	xSemaphoreGive(self->lock);

	xSemaphoreGive(self->free_slots);

	return REMOTE_INTERFACE_RET_OK;
}


static remote_interface_ret_t ri_write(RemoteInterface *iface, void *session, const void *buf, size_t len,
                                       uint32_t timeout_ms) {
	if (u_assert(iface != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(buf != NULL)) {
		return REMOTE_INTERFACE_RET_NULL;
	}
	RemoteInterfaceProxy *self = iface->parent;
	struct remote_interface_proxy_session *s = session;

	if (!session_valid(self, s) || len == 0 || len > self->conf.desc.mtu) {
		return REMOTE_INTERFACE_RET_BAD_ARG;
	}

	if (xMessageBufferSend(s->rx, buf, len, pdMS_TO_TICKS(timeout_ms)) != len) {
		return REMOTE_INTERFACE_RET_TIMEOUT;
	}
	return REMOTE_INTERFACE_RET_OK;
}


static remote_interface_ret_t ri_read(RemoteInterface *iface, void *session, void *buf, size_t size, size_t *len,
                                      uint32_t timeout_ms) {
	if (u_assert(iface != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len != NULL)) {
		return REMOTE_INTERFACE_RET_NULL;
	}
	RemoteInterfaceProxy *self = iface->parent;
	struct remote_interface_proxy_session *s = session;

	if (!session_valid(self, s) || size < self->conf.desc.mtu) {
		return REMOTE_INTERFACE_RET_BAD_ARG;
	}

	*len = xMessageBufferReceive(s->tx, buf, size, pdMS_TO_TICKS(timeout_ms));
	if (*len == 0) {
		return REMOTE_INTERFACE_RET_TIMEOUT;
	}

	xSemaphoreTake(self->lock, portMAX_DELAY);
	s->tx_pending--;
	xSemaphoreGive(self->lock);

	return REMOTE_INTERFACE_RET_OK;
}


static const struct remote_interface_vmt remote_interface_proxy_vmt = {
	.open = ri_open,
	.close = ri_close,
	.write = ri_write,
	.read = ri_read,
};


/**********************************************************************************************************************
 * Service lifecycle
 **********************************************************************************************************************/

remote_interface_proxy_ret_t remote_interface_proxy_init(RemoteInterfaceProxy *self,
                                                         const struct remote_interface_proxy_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return REMOTE_INTERFACE_PROXY_RET_NULL;
	}
	memset(self, 0, sizeof(RemoteInterfaceProxy));
	memcpy(&self->conf, conf, sizeof(struct remote_interface_proxy_conf));

	if (self->conf.desc.protocol == NULL ||
	    self->conf.desc.mtu == 0 ||
	    self->conf.desc.max_sessions == 0 ||
	    self->conf.queue_len == 0) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("invalid configuration"));
		return REMOTE_INTERFACE_PROXY_RET_BAD_ARG;
	}

	self->lock = xSemaphoreCreateMutex();
	self->free_slots = xSemaphoreCreateCounting(self->conf.desc.max_sessions, self->conf.desc.max_sessions);
	self->sessions = calloc(self->conf.desc.max_sessions, sizeof(struct remote_interface_proxy_session));
	if (self->lock == NULL || self->free_slots == NULL || self->sessions == NULL) {
		goto err;
	}

	/* Each message is stored along with its length. */
	size_t buf_size = self->conf.queue_len * (self->conf.desc.mtu + sizeof(configMESSAGE_BUFFER_LENGTH_TYPE));
	for (uint8_t i = 0; i < self->conf.desc.max_sessions; i++) {
		self->sessions[i].parent = self;
		self->sessions[i].dgram.vmt = &remote_interface_proxy_datagram_vmt;
		self->sessions[i].dgram.parent = &self->sessions[i];
		self->sessions[i].rx = xMessageBufferCreate(buf_size);
		self->sessions[i].tx = xMessageBufferCreate(buf_size);
		if (self->sessions[i].rx == NULL || self->sessions[i].tx == NULL) {
			goto err;
		}
	}

	self->iface.vmt = &remote_interface_proxy_vmt;
	self->iface.parent = self;
	self->iface.desc = &self->conf.desc;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("protocol '%s', %u sessions, mtu %u"),
	      self->conf.desc.protocol, (unsigned)self->conf.desc.max_sessions, (unsigned)self->conf.desc.mtu);
	return REMOTE_INTERFACE_PROXY_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate resources"));
	remote_interface_proxy_free(self);
	return REMOTE_INTERFACE_PROXY_RET_NOMEM;
}


remote_interface_proxy_ret_t remote_interface_proxy_free(RemoteInterfaceProxy *self) {
	if (u_assert(self != NULL)) {
		return REMOTE_INTERFACE_PROXY_RET_NULL;
	}

	if (self->sessions != NULL) {
		for (uint8_t i = 0; i < self->conf.desc.max_sessions; i++) {
			if (self->sessions[i].rx != NULL) {
				vMessageBufferDelete(self->sessions[i].rx);
			}
			if (self->sessions[i].tx != NULL) {
				vMessageBufferDelete(self->sessions[i].tx);
			}
		}
		free(self->sessions);
		self->sessions = NULL;
	}
	if (self->free_slots != NULL) {
		vSemaphoreDelete(self->free_slots);
		self->free_slots = NULL;
	}
	if (self->lock != NULL) {
		vSemaphoreDelete(self->lock);
		self->lock = NULL;
	}

	return REMOTE_INTERFACE_PROXY_RET_OK;
}
