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

#include "blake2.h"

#include "nbus-root.h"

#define MODULE_NAME "nbus-root"


static void nbus_receive_task(void *p) {
	NbusRoot *self = p;

	while (true) {
		nbus_endpoint_t ep = 0;
		size_t len = 0;
		nbus_ret_t ret = nbus_channel_receive(&self->channel, &ep, &self->buf, sizeof(self->buf), &len, 1000);
		if (ret == NBUS_RET_OK) {
			nbus_channel_send(&self->channel, ep, &self->buf, len);
		}
	}

	vTaskDelete(NULL);
}


static nbus_short_id_t prepare_short_id(const void *anything, size_t len) {
	nbus_short_id_t short_id = 0;
	blake2s_state b2;
	blake2s_init(&b2, sizeof(nbus_short_id_t));
	blake2s_update(&b2, anything, len);
	blake2s_update(&b2, "root", 4);
	blake2s_final(&b2, &short_id, sizeof(short_id));
	return short_id;
}


nbus_ret_t nbus_root_init(NbusRoot *self, Nbus *nbus, const void *anything, size_t len) {
	memset(self, 0, sizeof(NbusRoot));

	self->nbus = nbus;

	/* The root channel is a bit specific, its short-id is prepared from a (presumably) unique
	 * buffer anything. */
	nbus_channel_init(&self->channel, "root");
	nbus_channel_set_explicit_short_id(&self->channel, prepare_short_id(anything, len));
	nbus_add_channel(self->nbus, &self->channel);

	xTaskCreate(nbus_receive_task, "nbus-root-rx", configMINIMAL_STACK_SIZE + 64, (void *)self, 1, &(self->receive_task));
	if (self->receive_task == NULL) {
		return NBUS_RET_FAILED;
	}

	return NBUS_RET_OK;
}



