/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A CBOR formatted MIB in a flash device
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/flash.h>


typedef enum {
	FLASH_CBOR_MIB_RET_OK = 0,
	FLASH_CBOR_MIB_RET_FAILED,
} flash_cbor_mib_ret_t;

typedef struct {
	Flash *flash;
} FlashCborMib;


flash_cbor_mib_ret_t flash_cbor_mib_init(FlashCborMib *self, Flash *flash);
flash_cbor_mib_ret_t flash_cbor_mib_free(FlashCborMib *self);

