/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Block memory device interface descriptor
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "block.h"


block_ret_t block_init(Block *self, block_vmt_t *vmt) {
	memset(self, 0, sizeof(Block));
	self->vmt = vmt;
	return BLOCK_RET_OK;
}


block_ret_t block_free(Block *self) {
	(void)self;
	return BLOCK_RET_OK;
}

