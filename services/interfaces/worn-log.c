/* SPDX-License-Identifier: BSD-2-Clause
 *
 * WORN log interface (write-once, read never)
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "worn-log.h"


worn_ret_t worn_init(Worn *self, worn_vmt_t *vmt) {
	memset(self, 0, sizeof(Worn));
	self->vmt = vmt;
	return WORN_RET_OK;
}


worn_ret_t worn_free(Worn *self) {
	(void)self;
	return WORN_RET_OK;
}

