/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NBUS flash access client service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <main.h>
#include <interfaces/flash.h>
#include <interfaces/datagram.h>


/* Buffer holding a single request or response, matching NBUS_FLASH_TX_BUF_LEN/NBUS_FLASH_RX_BUF_LEN. */
#define NBUS_FLASH_CLIENT_BUF_LEN 320
/* Maximum data length of a single read or write request, matching NBUS_FLASH_BLOCK_LEN. */
#define NBUS_FLASH_CLIENT_BLOCK_LEN 256
#define NBUS_FLASH_CLIENT_RETRIES 3
#define NBUS_FLASH_CLIENT_NAME_LEN 16


typedef enum {
	NBUS_FLASH_CLIENT_RET_OK = 0,
	NBUS_FLASH_CLIENT_RET_FAILED,
	NBUS_FLASH_CLIENT_RET_NULL,
	NBUS_FLASH_CLIENT_RET_NOT_FOUND,
	NBUS_FLASH_CLIENT_RET_TIMEOUT,
} nbus_flash_client_ret_t;


struct nbus_flash_client_conf {
	/**
	 * A bound and connected nbus2 socket talking to the remote nbus-flash service. It must have a receive
	 * timeout set (see nbus_socket_set_timeout()), a lost request or response is retried after the timeout.
	 */
	Datagram *d;
};


/**
 * A client of a remote nbus-flash service. A single partition of the remote service is opened at a time and
 * accessed through the flash interface. The client is not thread safe, it must be used from a single thread.
 */
typedef struct {
	struct nbus_flash_client_conf conf;

	/* Flash interface of the opened remote partition. */
	Flash flash;

	/* Session of the opened partition, 0 if none is opened. */
	uint32_t sid;
	char name[NBUS_FLASH_CLIENT_NAME_LEN];
	size_t size;

	/* Identifier of the last request, echoed in its responses. */
	uint32_t req_id;

	/* Request and response buffer. */
	uint8_t buf[NBUS_FLASH_CLIENT_BUF_LEN];

	/* A single block read ahead, a reader doing many small sequential reads (eg. a CBOR parser) does not need a
	 * request for each of them. */
	uint8_t cache[NBUS_FLASH_CLIENT_BLOCK_LEN];
	size_t cache_addr;
	size_t cache_len;
} NbusFlashClient;


/**
 * @brief Initialize the NBUS flash access client
 *
 * @param self Preallocated instance.
 * @param conf Service configuration. The structure is copied.
 *
 * @return NBUS_FLASH_CLIENT_RET_NULL on a NULL argument, NBUS_FLASH_CLIENT_RET_OK otherwise.
 */
nbus_flash_client_ret_t nbus_flash_client_init(NbusFlashClient *self, const struct nbus_flash_client_conf *conf);

nbus_flash_client_ret_t nbus_flash_client_free(NbusFlashClient *self);

/**
 * @brief Get the name of a partition of the remote service
 *
 * The partitions are listed again on each call.
 *
 * @param i Index of the partition.
 * @param name Returns the partition name.
 * @param size Size of the @p name buffer.
 *
 * @return NBUS_FLASH_CLIENT_RET_NOT_FOUND if there is no partition @p i, NBUS_FLASH_CLIENT_RET_FAILED on error,
 *         NBUS_FLASH_CLIENT_RET_OK otherwise.
 */
nbus_flash_client_ret_t nbus_flash_client_list(NbusFlashClient *self, uint32_t i, char *name, size_t size);

/**
 * @brief Open a partition of the remote service
 *
 * A partition opened before is closed first. The partition is then accessed through the flash interface
 * (@p self->flash) until nbus_flash_client_close() is called.
 *
 * @return NBUS_FLASH_CLIENT_RET_FAILED if the partition cannot be opened, NBUS_FLASH_CLIENT_RET_OK otherwise.
 */
nbus_flash_client_ret_t nbus_flash_client_open(NbusFlashClient *self, const char *name);

nbus_flash_client_ret_t nbus_flash_client_close(NbusFlashClient *self);
