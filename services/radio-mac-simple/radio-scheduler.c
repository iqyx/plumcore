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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

#include "interfaces/radio.h"
#include "interfaces/radio-mac/host.h"
#include "crc.h"
#include "fec_golay.h"

#include "radio-mac-simple.h"
#include "radio-scheduler.h"

#define MODULE_NAME "mac-simple"


static mac_simple_ret_t rmac_slot_exec(MacSimple *self, struct rmac_slot *slot) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(self != NULL, MAC_SIMPLE_RET_BAD_ARG);

	/* Configure the radio. */
	radio_set_sync(self->radio, self->network_id, MAC_SIMPLE_NETWORK_ID_SIZE);

	/* Check for the time match. Adjust the negative delay to get exact match. */
	/** @todo */
	self->slot_start_time_error_ema_us = (self->slot_start_time_error_ema_us * 15 + (slot->slot_start_us - mac_simple_time(self))) / 16;

	mac_simple_ret_t r = MAC_SIMPLE_RET_OK;

	switch (slot->type) {
		case RMAC_SLOT_TYPE_RX_SEARCH:
		case RMAC_SLOT_TYPE_RX_UNMANAGED:
		case RMAC_SLOT_TYPE_RX_UNICAST: {
			struct radio_scheduler_rxpacket packet = {0};
			iradio_ret_t ret = radio_receive(self->radio, packet.buf, sizeof(packet.buf), &packet.len, &packet.rxparams, slot->slot_length_us);
			if (ret == IRADIO_RET_OK) {
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("rx packet len=%d rssi=%d"), packet.len, packet.rxparams.rssi_dbm);

				/* Put the packet in the rxqueue. If the queue is full, return FAILED. */
				if (xQueueSend(self->radio_scheduler_rxqueue, &packet, 0) == pdTRUE) {
					r = MAC_SIMPLE_RET_OK;
				}

				/* Packet received. If not in the low power mode, now is the
				 * right time to send packets to low power neighbors. */
				if (self->low_power == false) {
					struct rmac_slot tx_slot = {
						.slot_start_us = mac_simple_time(self) + 3000ull,
						.slot_length_us = 100000ul,
						.type = RMAC_SLOT_TYPE_TX_BROADCAST,
						.peer_id = 0,
					};
					rmac_slot_queue_insert(&self->slot_queue, &tx_slot);
				}
			} else if (ret == IRADIO_RET_TIMEOUT) {
				r = MAC_SIMPLE_RET_NOOP;
			}

			break;
		}
		case RMAC_SLOT_TYPE_TX_BROADCAST:
		case RMAC_SLOT_TYPE_TX_CONTROL:
		case RMAC_SLOT_TYPE_TX_UNICAST: {
			struct radio_scheduler_txpacket packet = {0};
			if (xQueueReceive(self->radio_scheduler_txqueue, &packet, 0) == pdTRUE) {
				struct iradio_send_params params = {0};
				if (radio_send(self->radio, packet.buf, packet.len, &params) != IRADIO_RET_OK) {
					r = MAC_SIMPLE_RET_FAILED;
				}

				/* Packet sent. For now, open a short RX slot. */
				if (self->low_power) {
					struct rmac_slot rx_slot = {
						.slot_start_us = mac_simple_time(self),
						.slot_length_us = 20000ul,
						.type = RMAC_SLOT_TYPE_RX_UNMANAGED,
						.peer_id = 0,
					};
					rmac_slot_queue_insert(&self->slot_queue, &rx_slot);
				}
			} else {
				r = MAC_SIMPLE_RET_NOOP;
			}

			break;
		}
		default:
			/* Do nothing and do not turn on the radio. */
			break;
	}

	return r;
}


/* Old implementation. A simple transmit/receive window scheduler. */
static void radio_scheduler_task(void *p) {
	MacSimple *self = (MacSimple *)p;

	while (true) {
		struct rmac_slot slot = {0};
		if (rmac_slot_queue_peek(&self->slot_queue, &slot) != MAC_SIMPLE_RET_OK) {
			/* No scheduled slot in the queue. This is unusual as there should
			 * always be some slots scheduled. Wait a bit and retry. */
			// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("noslot"));
			vTaskDelay(10);
			continue;
		}

		uint64_t time = mac_simple_time(self);

		if ((slot.slot_start_us + slot.slot_length_us) <= time) {
			/* We missed the whole slot. Discard it and try to catch the
			 * next one. */
			rmac_slot_queue_remove(&self->slot_queue, &slot);
			// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("miss"));
			continue;
		}

		if (slot.slot_start_us > time && ((slot.slot_start_us - time) > 2000)) {
			/* There is at least one slot scheduled which is approaching slowly.
			 * It is still at least 2ms in the future. Wait half the remaining time. */
			uint32_t w = (slot.slot_start_us - time) / 1000 / 4;
			if (w > 10) {
				/* But do not wait too much. */
				w = 10;
			}
			// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("wait %u"), w);
			vTaskDelay(w);
			continue;
		}

		/* The slot is lesser than 2ms in the future. There is no such precise wait
		 * supported by the RTOS, busy-wait the rest instead. */
		while (true) {
			time = mac_simple_time(self);
			if (time >= (slot.slot_start_us - 0)) {
				break;
			}
		}

		if (slot.slot_start_us <= time) {
			/* We hit the slot exactly or we missed the start a bit and the slot
			 * is still ongoing. */
			uint64_t slot_expected_end_us = slot.slot_start_us + slot.slot_length_us;
			uint32_t len_remaining = slot_expected_end_us - time;
			if (len_remaining < 2000) {
				/* There is not much time left. It is not feasible to start
				 * the slot if less than 2ms is remaining. Discard it and retry. */
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("miss-start"));
				rmac_slot_queue_remove(&self->slot_queue, &slot);
				continue;
			}

			/* Finally we managed to get to the beginning. Remove the slot from
			 * the queue and start the radio. Shorten the slot a bit in order
			 * to stop listening in time. We are not expecting any packet just
			 * before the slot ends. */
			rmac_slot_queue_remove(&self->slot_queue, &slot);
			/** @todo adaptive based on slot_expected_end */
			slot.slot_length_us = len_remaining - 3000;

			// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("exact"));
			rmac_slot_exec(self, &slot);
		}

		if (self->state == MAC_SIMPLE_STATE_STOP_REQ) {
			break;
		}
	}

	vTaskDelete(NULL);
}


static void slot_scheduler_task(void *p) {
	MacSimple *self = (MacSimple *)p;

	vTaskDelay(1000);
	uint64_t next_slot = mac_simple_time(self);
	while (true) {

		uint64_t time = mac_simple_time(self);
		if (self->low_power) {
			/* In the low power mode, we do not want to receive all the time. Instead
			 * the transmit queue is checked occasionally and packets are transmitted
			 * until the queue is empty. Then a reception window is opened for a
			 * specified time. If a packet is received, it is lengthened until no more
			 * packets are received. Radio is then turned off to preserve power. */

			while (rmac_slot_queue_len(&self->slot_queue) < 5 && next_slot < (time + 2000000ull)) {
				struct rmac_slot slot = {
					.slot_start_us = next_slot,
					.slot_length_us = 100000ul,
					.type = RMAC_SLOT_TYPE_TX_BROADCAST,
					.peer_id = 0,
				};
				rmac_slot_queue_insert(&self->slot_queue, &slot);
				next_slot += 200000ull;
			}
		} else {
			/* In the full-power mode, wait for a packet. Match it to the corresponding
			 * neighbor and check if it is in the low power mode. If yes, this is
			 * the right time to respond. Get up to n packets from the neighbor
			 * queue and send them. If the neighbor is in full power mode too,
			 * basically the same thing could be done except the timing is not
			 * so strict. */

			while (rmac_slot_queue_len(&self->slot_queue) < 5 && next_slot < (time + 2000000ull)) {
				struct rmac_slot slot = {
					.slot_start_us = next_slot,
					.slot_length_us = 200000ul,
					.type = RMAC_SLOT_TYPE_RX_UNMANAGED,
					.peer_id = 0,
				};
				rmac_slot_queue_insert(&self->slot_queue, &slot);
				next_slot += 200000ull;
			}
		}
		vTaskDelay(100);
	}

	vTaskDelete(NULL);
}


mac_simple_ret_t radio_scheduler_start(MacSimple *self) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);

	self->radio_scheduler_rxqueue = xQueueCreate(2, sizeof(struct radio_scheduler_rxpacket));
	if(self->radio_scheduler_rxqueue == NULL) {
		return MAC_SIMPLE_RET_FAILED;
	}

	self->radio_scheduler_txqueue = xQueueCreate(2, sizeof(struct radio_scheduler_txpacket));
	if(self->radio_scheduler_txqueue == NULL) {
		return MAC_SIMPLE_RET_FAILED;
	}

	if (rmac_slot_queue_init(&self->slot_queue, 31) != MAC_SIMPLE_RET_OK) {
		return MAC_SIMPLE_RET_FAILED;
	}

	self->state = MAC_SIMPLE_STATE_RUNNING;
	xTaskCreate(radio_scheduler_task, "rmac-radio-sch", configMINIMAL_STACK_SIZE + 1024, (void *)self, 3, &(self->radio_scheduler_task));
	if (self->radio_scheduler_task == NULL) {
		return MAC_SIMPLE_RET_FAILED;
	}

	xTaskCreate(slot_scheduler_task, "rmac-radio-ssch", configMINIMAL_STACK_SIZE + 1024, (void *)self, 1, &(self->slot_scheduler_task));
	if (self->radio_scheduler_task == NULL) {
		return MAC_SIMPLE_RET_FAILED;
	}

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t radio_scheduler_tx(MacSimple *self, struct radio_scheduler_txpacket *packet) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(packet != NULL, MAC_SIMPLE_RET_BAD_ARG);

	if (xQueueSend(self->radio_scheduler_txqueue, packet, portMAX_DELAY) != pdTRUE) {
		return MAC_SIMPLE_RET_FAILED;
	}

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t radio_scheduler_rx(MacSimple *self, struct radio_scheduler_rxpacket *packet) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(packet != NULL, MAC_SIMPLE_RET_BAD_ARG);

	if (xQueueReceive(self->radio_scheduler_rxqueue, packet, portMAX_DELAY) != pdTRUE) {
		return MAC_SIMPLE_RET_FAILED;
	}

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t rmac_slot_queue_init(struct rmac_slot_queue *self, size_t size) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);

	memset(self, 0, sizeof(struct rmac_slot_queue));
	self->items = calloc(size, sizeof(struct rmac_slot));
	if (self->items == NULL) {
		return MAC_SIMPLE_RET_FAILED;
	}
	self->size = size;

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return MAC_SIMPLE_RET_FAILED;
	}

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t rmac_slot_queue_free(struct rmac_slot_queue *self) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);

	free(self->items);

	return MAC_SIMPLE_RET_OK;
}


void rmac_slot_queue_siftup(struct rmac_slot_queue *self, size_t index) {
	while (true) {
		if (index == 0) {
			break;
		}
		size_t index_parent = (index - 1) / 2;

		if (self->items[index_parent].slot_start_us > self->items[index].slot_start_us) {
			struct rmac_slot tmp = {0};
			memcpy(&tmp, &(self->items[index]), sizeof(struct rmac_slot));
			memcpy(&(self->items[index]), &(self->items[index_parent]), sizeof(struct rmac_slot));
			memcpy(&(self->items[index_parent]), &tmp, sizeof(struct rmac_slot));
			index = index_parent;
		} else {
			break;
		}
	}
}


void rmac_slot_queue_siftdown(struct rmac_slot_queue *self, size_t index) {
	while (true) {
		size_t index_left = index * 2 + 1;
		size_t index_right = index * 2 + 2;

		size_t index_smallest = index;
		if (index_left < self->len && self->items[index_left].slot_start_us < self->items[index_smallest].slot_start_us) {
			index_smallest = index_left;
		}
		if (index_right < self->len && self->items[index_right].slot_start_us < self->items[index_smallest].slot_start_us) {
			index_smallest = index_right;
		}

		if (index != index_smallest) {
			struct rmac_slot tmp = {0};
			memcpy(&tmp, &(self->items[index]), sizeof(struct rmac_slot));
			memcpy(&(self->items[index]), &(self->items[index_smallest]), sizeof(struct rmac_slot));
			memcpy(&(self->items[index_smallest]), &tmp, sizeof(struct rmac_slot));
			index = index_smallest;
		} else {
			break;
		}

	}
}


mac_simple_ret_t rmac_slot_queue_insert(struct rmac_slot_queue *self, struct rmac_slot *slot) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(slot != NULL, MAC_SIMPLE_RET_BAD_ARG);

	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("insert s=%u l=%u ql=%u"), (uint32_t)slot->slot_start_us, slot->slot_length_us, self->len);

	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (self->len >= self->size) {
		/* Queue full. */
		xSemaphoreGive(self->lock);
		return MAC_SIMPLE_RET_FAILED;
	}

	memcpy(&(self->items[self->len]), slot, sizeof(struct rmac_slot));
	self->len++;
	rmac_slot_queue_siftup(self, self->len - 1);
	xSemaphoreGive(self->lock);

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t rmac_slot_queue_peek(struct rmac_slot_queue *self, struct rmac_slot *slot) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(slot != NULL, MAC_SIMPLE_RET_BAD_ARG);

	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (self->len == 0) {
		xSemaphoreGive(self->lock);
		return MAC_SIMPLE_RET_FAILED;
	}

	memcpy(slot, &(self->items[0]), sizeof(struct rmac_slot));
	xSemaphoreGive(self->lock);
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("peek s=%u l=%u"), (uint32_t)slot->slot_start_us, slot->slot_length_us);

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t rmac_slot_queue_remove(struct rmac_slot_queue *self, struct rmac_slot *slot) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(slot != NULL, MAC_SIMPLE_RET_BAD_ARG);

	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (self->len == 0) {
		xSemaphoreGive(self->lock);
		return MAC_SIMPLE_RET_FAILED;
	}

	memcpy(slot, &(self->items[0]), sizeof(struct rmac_slot));

	if (self->len > 1) {
		memcpy(&(self->items[0]), &(self->items[self->len - 1]), sizeof(struct rmac_slot));
		self->len--;
		rmac_slot_queue_siftdown(self, 0);
	} else {
		self->len--;
	}

	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("remove s=%u l=%u"), (uint32_t)slot->slot_start_us, slot->slot_length_us);
	xSemaphoreGive(self->lock);
	return MAC_SIMPLE_RET_OK;
}


size_t rmac_slot_queue_len(struct rmac_slot_queue *self) {
	ASSERT(self != NULL, 0);

	return self->len;
}


