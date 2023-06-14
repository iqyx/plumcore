/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nwDAQ bus implementation, peripheral-side only
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "nbus-log.h"



nbus_ret_t nbus_log_init(NbusLog *self, NbusChannel *parent) {
	memset(self, 0, sizeof(NbusLog));

	nbus_channel_init(&self->channel, "log");
	nbus_channel_set_parent(&self->channel, parent);
	nbus_add_channel(parent->nbus, &self->channel);

	return NBUS_RET_OK;
}


nbus_ret_t nbus_log_free(NbusLog *self) {
	(void)self;
	return NBUS_RET_OK;
}

