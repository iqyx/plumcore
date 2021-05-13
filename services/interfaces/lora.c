/* SPDX-License-Identifier: BSD-2-Clause
 *
 * LoRaWAN modem generic interface
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "interfaces/lora.h"


lora_ret_t lora_init(LoRa *self, const lora_vmt_t *vmt) {
	memset(self, 0, sizeof(LoRa));
	self->vmt = vmt;
	return LORA_RET_OK;
}


lora_ret_t lora_free(LoRa *self) {
	(void)self;
	return LORA_RET_OK;
}

