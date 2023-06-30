/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A CBOR formatted MIB in a flash device
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include <main.h>

#include <interfaces/flash.h>

#include "flash-cbor-mib.h"

#define MODULE_NAME "flash-cbor-mib"


flash_cbor_mib_ret_t flash_cbor_mib_init(FlashCborMib *self, Flash *flash) {
	if (u_assert(self != NULL) ||
	    u_assert(flash != NULL)) {
		return FLASH_CBOR_MIB_RET_FAILED;
	}
	memset(self, 0, sizeof(FlashCborMib));
	self->flash = flash;

	return FLASH_CBOR_MIB_RET_OK;
}


flash_cbor_mib_ret_t flash_cbor_mib_free(FlashCborMib *self) {
	if (u_assert(self != NULL)) {
		return FLASH_CBOR_MIB_RET_FAILED;
	}

	return FLASH_CBOR_MIB_RET_OK;
}






