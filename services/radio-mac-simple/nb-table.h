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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>


typedef enum {
	NBTABLE_RET_OK = 0,
	NBTABLE_RET_FAILED,
	NBTABLE_RET_NULL,
	NBTABLE_RET_BAD_ARG,
} nbtable_ret_t;

struct nbtable_item {
	bool used;
	uint32_t id;
	float rssi_dbm;
	uint32_t rxpackets;
	uint32_t rxbytes;
	uint32_t rxmissed;
	uint32_t txpackets;
	uint32_t txbytes;
	uint8_t counter;
	uint8_t time;
};


typedef struct {
	size_t count;
	struct nbtable_item *items;

} NbTable;


nbtable_ret_t nbtable_init(NbTable *self, size_t items);
nbtable_ret_t nbtable_free(NbTable *self);

struct nbtable_item *nbtable_find_id(NbTable *self, uint32_t id);
struct nbtable_item *nbtable_find_empty(NbTable *self);
struct nbtable_item *nbtable_find_or_add_id(NbTable *self, uint32_t id);

nbtable_ret_t nbtable_init_nb(NbTable *self, struct nbtable_item *item, bool used);
nbtable_ret_t nbtable_free_nb(NbTable *self, struct nbtable_item *item);
nbtable_ret_t nbtable_update_rx_counter(NbTable *self, struct nbtable_item *item, uint8_t counter, size_t len);
nbtable_ret_t nbtable_update_rssi(NbTable *self, struct nbtable_item *item, float rssi_dbm);

