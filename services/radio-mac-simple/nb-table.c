/*
 * neighbor table management
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

#include "radio-mac-simple.h"
#include "nb-table.h"

#define MODULE_NAME "mac-simple"


nbtable_ret_t nbtable_init(NbTable *self, size_t items) {
	ASSERT(self != NULL, NBTABLE_RET_FAILED);

	memset(self, 0, sizeof(NbTable));

	self->items = calloc(items, sizeof(struct nbtable_item));
	if (self->items == NULL) {
		return NBTABLE_RET_FAILED;
	}
	self->count = items;
	for (size_t i = 0; i < items; i++) {
		nbtable_init_nb(self, &(self->items[i]), false);
	}

	return NBTABLE_RET_OK;
}


nbtable_ret_t nbtable_free(NbTable *self) {
	ASSERT(self != NULL, NBTABLE_RET_NULL);

	free(self->items);
	self->count = 0;

	return NBTABLE_RET_OK;
}


struct nbtable_item *nbtable_find_id(NbTable *self, uint32_t id) {
	ASSERT(self != NULL, NULL);

	for (size_t i = 0; i < self->count; i++) {
		if (self->items[i].used && self->items[i].id == id) {
			return &(self->items[i]);
		}
	}
	return NULL;
}


struct nbtable_item *nbtable_find_empty(NbTable *self) {
	ASSERT(self != NULL, NULL);

	for (size_t i = 0; i < self->count; i++) {
		if (self->items[i].used ==false) {
			return &(self->items[i]);
		}
	}
	return NULL;
}


struct nbtable_item *nbtable_find_or_add_id(NbTable *self, uint32_t id) {
	ASSERT(self != NULL, NULL);

	struct nbtable_item *item = nbtable_find_id(self, id);
	if (item == NULL) {
		/* If not item is found, try to find an empty one to add it. */
		item = nbtable_find_empty(self);
		if (item == NULL) {
			/* Tabel is full. */
			return NULL;
		}
		nbtable_init_nb(self, item, true);
		item->id = id;
	}
	return item;
}


nbtable_ret_t nbtable_init_nb(NbTable *self, struct nbtable_item *item, bool used) {
	ASSERT(self != NULL, NBTABLE_RET_NULL);
	ASSERT(item != NULL, NBTABLE_RET_BAD_ARG);

	memset(item, 0, sizeof(struct nbtable_item));
	item->used = used;

	return NBTABLE_RET_OK;
}


nbtable_ret_t nbtable_free_nb(NbTable *self, struct nbtable_item *item) {
	ASSERT(self != NULL, NBTABLE_RET_NULL);
	ASSERT(item != NULL, NBTABLE_RET_BAD_ARG);

	memset(item, 0, sizeof(struct nbtable_item));
	item->used = false;

	return NBTABLE_RET_OK;
}


nbtable_ret_t nbtable_update_rx_counter(NbTable *self, struct nbtable_item *item, uint8_t counter, size_t len) {
	ASSERT(self != NULL, NBTABLE_RET_NULL);

	/* Check if the packet counter was incremented by 1. Compute number of missed packets. */
	int8_t counter_diff = counter - item->counter;
	if (counter_diff > 0) {
		item->rxmissed += counter_diff - 1;
		item->counter = counter;
	}

	/* Update neighbor statistics and reset the item age value. */
	item->rxpackets++;
	item->rxbytes += len;
	item->time = 0;

	return NBTABLE_RET_OK;
}


nbtable_ret_t nbtable_update_rssi(NbTable *self, struct nbtable_item *item, float rssi_dbm) {
	ASSERT(self != NULL, NBTABLE_RET_NULL);

	/* Exponential moving average of the packet RSSI. */
	item->rssi_dbm = (15 * item->rssi_dbm + rssi_dbm) / 16.0;

	return NBTABLE_RET_OK;
}


