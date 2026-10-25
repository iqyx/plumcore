/* SPDX-License-Identifier: BSD-2-Clause
 *
 * NBUS flash access service
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include "config.h"
#include <interfaces/flash.h>
#include <interfaces/datagram.h>


#define NBUS_FLASH_MAIN_EP 1
#define NBUS_FLASH_RX_BUF_LEN 320
#define NBUS_FLASH_TX_BUF_LEN 320
#define NBUS_FLASH_INTERFACE_NAME "flash"
#define NBUS_FLASH_INTERFACE_VERSION "1.0.0"
#define NBUS_FLASH_BLOCK_LEN 256
#define NBUS_FLASH_SESSIONS CONFIG_SERVICE_NBUS_FLASH_SESSIONS

typedef enum {
	NBUS_FLASH_RET_OK = 0,
	NBUS_FLASH_RET_FAILED,
} nbus_flash_ret_t;


/* A flash partition opened by a client. The session is free if flash is NULL. */
struct nbus_flash_session {
	uint32_t sid;
	Flash *flash;

	/* Sequence number of the last request using the session, the least recently used one is reused when all
	 * sessions are taken. */
	uint32_t used;
};


typedef struct nbus_flash {
	Datagram *d;
	TaskHandle_t nbus_task;
	uint8_t rx_buf[NBUS_FLASH_RX_BUF_LEN];
	uint8_t tx_buf[NBUS_FLASH_TX_BUF_LEN];

	/* Source of the request being processed, the response is sent back there. */
	uint8_t src_addr[4];
	uint32_t src_port;

	struct nbus_flash_session sessions[NBUS_FLASH_SESSIONS];
	uint32_t last_sid;
	uint32_t use_seq;
} NbusFlash;


nbus_flash_ret_t nbus_flash_init(NbusFlash *self, Datagram *d);
nbus_flash_ret_t nbus_flash_free(NbusFlash *self);
