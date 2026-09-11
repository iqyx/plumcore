/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NBUS flash access proxy service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Transparent proxy for the nbus-flash access protocol. It exposes the same client-facing interface as
 * the nbus-flash service (a Datagram carrying CBOR flash commands) but, instead of accessing a local
 * Flash device, it forwards every request to a remote nbus-flash service over a second Datagram and
 * relays the responses back.
 *
 * The two Datagrams are usually a local endpoint the client connects to (e.g. a proto-dgble tunnel
 * endpoint reached over BLE) and a connected nbus2 socket talking to the remote service. The payload is
 * never decoded, so the proxy stays transparent to the protocol; the nbus-flash reply echoes the flash
 * address, which lets the client match a response to its request even though the two relay directions
 * run independently.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/datagram.h>

#include "nbus-flash-proxy.h"

#define MODULE_NAME "nbus-flash-proxy"


/**********************************************************************************************************************
 * Relay tasks
 **********************************************************************************************************************/

/* Forward requests from the client to the remote nbus-flash service. */
static void c2r_task(void *p) {
	NbusFlashProxy *self = p;

	self->c2r_running = true;
	while (self->can_run) {
		size_t len = sizeof(self->c2r_buf);
		if (self->conf.client->vmt->read(self->conf.client, self->c2r_buf, &len, NULL) == DATAGRAM_RET_OK) {
			datagram_ret_t ret = self->conf.remote->vmt->write(self->conf.remote, self->c2r_buf, len, NULL);
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("client -> remote: %u bytes (write ret %d)"),
			      (unsigned)len, (int)ret);
		}
	}
	self->c2r_running = false;

	vTaskDelete(NULL);
}


/* Forward responses from the remote nbus-flash service back to the client. */
static void r2c_task(void *p) {
	NbusFlashProxy *self = p;

	self->r2c_running = true;
	while (self->can_run) {
		size_t len = sizeof(self->r2c_buf);
		if (self->conf.remote->vmt->read(self->conf.remote, self->r2c_buf, &len, NULL) == DATAGRAM_RET_OK) {
			datagram_ret_t ret = self->conf.client->vmt->write(self->conf.client, self->r2c_buf, len, NULL);
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("remote -> client: %u bytes (write ret %d)"),
			      (unsigned)len, (int)ret);
		}
	}
	self->r2c_running = false;

	vTaskDelete(NULL);
}


/**********************************************************************************************************************
 * Service lifecycle
 **********************************************************************************************************************/

nbus_flash_proxy_ret_t nbus_flash_proxy_init(NbusFlashProxy *self, const struct nbus_flash_proxy_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return NBUS_FLASH_PROXY_RET_NULL;
	}
	memset(self, 0, sizeof(NbusFlashProxy));
	memcpy(&self->conf, conf, sizeof(struct nbus_flash_proxy_conf));

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return NBUS_FLASH_PROXY_RET_OK;
}


nbus_flash_proxy_ret_t nbus_flash_proxy_free(NbusFlashProxy *self) {
	if (u_assert(self != NULL)) {
		return NBUS_FLASH_PROXY_RET_FAILED;
	}

	return NBUS_FLASH_PROXY_RET_OK;
}


nbus_flash_proxy_ret_t nbus_flash_proxy_start(NbusFlashProxy *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->conf.client != NULL) ||
	    u_assert(self->conf.remote != NULL)) {
		return NBUS_FLASH_PROXY_RET_FAILED;
	}

	self->can_run = true;
	xTaskCreate(c2r_task, "nbus-fl-px-c2r", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->c2r_task));
	if (self->c2r_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the request relay task"));
		goto err;
	}

	xTaskCreate(r2c_task, "nbus-fl-px-r2c", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->r2c_task));
	if (self->r2c_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create the response relay task"));
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("started"));
	return NBUS_FLASH_PROXY_RET_OK;
err:
	/* Not fully started, stop everything. */
	nbus_flash_proxy_stop(self);
	return NBUS_FLASH_PROXY_RET_FAILED;
}


nbus_flash_proxy_ret_t nbus_flash_proxy_stop(NbusFlashProxy *self) {
	if (u_assert(self != NULL)) {
		return NBUS_FLASH_PROXY_RET_FAILED;
	}

	/* Signal both tasks to exit. Each may stay blocked in a datagram read until the next datagram
	 * arrives on its side; it exits on the following loop check. */
	self->can_run = false;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));
	return NBUS_FLASH_PROXY_RET_OK;
}
