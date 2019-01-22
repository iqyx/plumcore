/*
 * rMAC radio scheduler
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

#include "interfaces/radio-mac/descriptor.h"


struct radio_scheduler_rxpacket {
	uint8_t buf[256];
	size_t len;
	struct iradio_receive_params rxparams;
};

struct radio_scheduler_txpacket {
	uint8_t buf[256];
	size_t len;
};


mac_simple_ret_t radio_scheduler_tx(MacSimple *self, struct radio_scheduler_txpacket *packet);
mac_simple_ret_t radio_scheduler_rx(MacSimple *self, struct radio_scheduler_rxpacket *packet);


mac_simple_ret_t rmac_slot_queue_init(struct rmac_slot_queue *self, size_t size);
mac_simple_ret_t rmac_slot_queue_free(struct rmac_slot_queue *self);
void rmac_slot_queue_siftup(struct rmac_slot_queue *self, size_t index);
void rmac_slot_queue_siftdown(struct rmac_slot_queue *self, size_t index);
mac_simple_ret_t rmac_slot_queue_insert(struct rmac_slot_queue *self, struct rmac_slot *slot);
mac_simple_ret_t rmac_slot_queue_peek(struct rmac_slot_queue *self, struct rmac_slot *slot);
mac_simple_ret_t rmac_slot_queue_remove(struct rmac_slot_queue *self, struct rmac_slot *slot);
size_t rmac_slot_queue_len(struct rmac_slot_queue *self);
