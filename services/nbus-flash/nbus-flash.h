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
#include <services/nbus/nbus.h>
#include <interfaces/flash.h>


#define NBUS_FLASH_MAIN_EP 1
#define NBUS_FLASH_RX_BUF_LEN 320
#define NBUS_FLASH_TX_BUF_LEN 320
#define NBUS_FLASH_INTERFACE_NAME "flash"
#define NBUS_FLASH_INTERFACE_VERSION "1.0.0"
#define NBUS_FLASH_BLOCK_LEN 256

typedef enum {
	NBUS_FLASH_RET_OK = 0,
	NBUS_FLASH_RET_FAILED,
} nbus_flash_ret_t;


typedef struct nbus_flash {
	NbusChannel channel;
	TaskHandle_t nbus_task;
	uint8_t rx_buf[NBUS_FLASH_RX_BUF_LEN];
	uint8_t tx_buf[NBUS_FLASH_TX_BUF_LEN];

	/* Single session support now. */
	uint32_t sid;
	Flash *flash;

} NbusFlash;


nbus_flash_ret_t nbus_flash_init(NbusFlash *self, NbusChannel *parent, const char *name);
nbus_flash_ret_t nbus_flash_free(NbusFlash *self);
