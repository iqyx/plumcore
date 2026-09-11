/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NBUS flash access proxy service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/datagram.h>


/* Scratch buffer for a single relayed datagram in each direction. Sized to hold a whole nbus-flash
 * request/response, matching NBUS_FLASH_TX_BUF_LEN/NBUS_FLASH_RX_BUF_LEN. */
#define NBUS_FLASH_PROXY_BUF_LEN 320


typedef enum {
	NBUS_FLASH_PROXY_RET_OK = 0,
	NBUS_FLASH_PROXY_RET_FAILED,
	NBUS_FLASH_PROXY_RET_NULL,
} nbus_flash_proxy_ret_t;


/* Service configuration passed to nbus_flash_proxy_init(). Not typedef'd per project policy. */
struct nbus_flash_proxy_conf {
	/**
	 * Client-facing datagram. Flash clients send their requests here and receive the relayed
	 * responses back on it (e.g. a proto-dgble tunnel endpoint reached over BLE).
	 */
	Datagram *client;

	/**
	 * Remote-facing datagram, a bound and connected nbus2 socket talking to the remote nbus-flash
	 * service the requests are forwarded to.
	 */
	Datagram *remote;
};


typedef struct {
	struct nbus_flash_proxy_conf conf;

	/* Scratch buffers, one per relay direction so both tasks may run concurrently. */
	uint8_t c2r_buf[NBUS_FLASH_PROXY_BUF_LEN];
	uint8_t r2c_buf[NBUS_FLASH_PROXY_BUF_LEN];

	/* The two relay directions run as independent tasks so neither can stall the other: the request
	 * path (client to remote) and the response path (remote to client) each block on their own read. */
	volatile bool can_run;
	volatile bool c2r_running;
	volatile bool r2c_running;
	TaskHandle_t c2r_task;
	TaskHandle_t r2c_task;
} NbusFlashProxy;


/**
 * @brief Initialize the NBUS flash access proxy
 *
 * Copies the configuration into the instance. Resources are allocated later in
 * nbus_flash_proxy_start().
 *
 * @param self Preallocated instance.
 * @param conf Service configuration. The structure is copied.
 *
 * @return NBUS_FLASH_PROXY_RET_NULL on a NULL argument, NBUS_FLASH_PROXY_RET_OK otherwise.
 */
nbus_flash_proxy_ret_t nbus_flash_proxy_init(NbusFlashProxy *self, const struct nbus_flash_proxy_conf *conf);

nbus_flash_proxy_ret_t nbus_flash_proxy_free(NbusFlashProxy *self);

/**
 * @brief Start relaying datagrams between the client and the remote nbus-flash service
 *
 * Starts the two relay tasks. Every datagram received from the client is forwarded verbatim to the
 * remote service and every datagram received from the remote service is forwarded verbatim back to
 * the client. The payload is not interpreted, so the proxy is transparent to the nbus-flash protocol.
 *
 * @return NBUS_FLASH_PROXY_RET_FAILED on error, NBUS_FLASH_PROXY_RET_OK otherwise.
 */
nbus_flash_proxy_ret_t nbus_flash_proxy_start(NbusFlashProxy *self);

nbus_flash_proxy_ret_t nbus_flash_proxy_stop(NbusFlashProxy *self);
