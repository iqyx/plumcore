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


static const char *nbus_log_descriptor[] = {
	"interface=log",
	"version=1.0.0",
	"request-stream=0",
};


static nbus_ret_t channel_rx_handler(void *context, uint8_t stream, void *buf, size_t len) {
	return NBUS_RET_OK;
}


static nbus_ret_t channel_descriptor(void *context, uint8_t id, void *buf, size_t *len) {
	NbusLog *self = (NbusLog *)context;
	switch (id) {
		case 0 ... 2:
			*len = strlcpy((char *)buf, nbus_log_descriptor[id], NBUS_DESCRIPTOR_LEN);
			return NBUS_RET_OK;
		case 3: {
			int ret = snprintf((char *)buf, NBUS_DESCRIPTOR_LEN, "parent=%08lx", (uint32_t)self->parent->short_id);
			if (ret > 0 && ret < NBUS_DESCRIPTOR_LEN) {
				*len = ret;
				return NBUS_RET_OK;
			}
			return NBUS_RET_FAILED;
		}
		case 4: {
			int ret = snprintf((char *)buf, NBUS_DESCRIPTOR_LEN, "name=%s", self->channel.name);
			if (ret > 0 && ret < NBUS_DESCRIPTOR_LEN) {
				*len = ret;
				return NBUS_RET_OK;
			}
			return NBUS_RET_FAILED;
		}
		default:
			return NBUS_RET_FAILED;
	}
	return NBUS_RET_FAILED;
}


nbus_ret_t nbus_log_init(NbusLog *self, const char *name, NbusChannel *parent) {
	memset(self, 0, sizeof(NbusLog));
	self->parent = parent;
	self->nbus = parent->parent;

	nbus_channel_init(&self->channel, name);
	nbus_channel_set_rx_handler(&self->channel, channel_rx_handler, self);
	nbus_channel_set_descriptor(&self->channel, channel_descriptor, self);
	nbus_channel_set_long_id(&self->channel, (const void *)&self->parent->short_id, sizeof(short_id_t));
	self->channel.peripheral = true;
	nbus_add_channel(self->nbus, &self->channel);

	return NBUS_RET_OK;
}


nbus_ret_t nbus_log_free(NbusLog *self) {

}

