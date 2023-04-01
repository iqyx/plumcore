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

#include "nbus-root.h"


static const char * const nbus_root_descriptor[] = {
	"name=root",
	"interface=none",
};


static nbus_ret_t channel_descriptor(void *context, uint8_t id, void *buf, size_t *len) {
	NbusRoot *self = (NbusRoot *)context;
	switch (id) {
		case 0 ... 1:
			*len = strlcpy((char *)buf, nbus_root_descriptor[id], NBUS_DESCRIPTOR_LEN);
			return NBUS_RET_OK;
		default:
			return NBUS_RET_FAILED;
	}
	return NBUS_RET_FAILED;
}


nbus_ret_t nbus_root_init(NbusRoot *self, Nbus *nbus, const void *anything, size_t len) {
	memset(self, 0, sizeof(NbusRoot));

	self->nbus = nbus;

	nbus_channel_init(&self->channel, "root");
	nbus_channel_set_descriptor_strings(&self->channel, nbus_root_descriptor, 2);
	nbus_channel_set_long_id(&self->channel, anything, len);
	self->channel.peripheral = true;
	nbus_add_channel(self->nbus, &self->channel);

	return NBUS_RET_OK;
}



