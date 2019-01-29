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
#include "rmac.h"

#include "interfaces/radio.h"
#include "interfaces/radio-mac/host.h"
#include "crc.h"
#include "fec_golay.h"


#define MODULE_NAME "rmac-rsch"
#define ASSERT(c, r) if (u_assert(c)) return r


static uint64_t rmac_time(Rmac *self) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	if (self->clock) {
		struct timespec t;
		iclock_get(self->clock, &t);
		return (uint64_t)t.tv_sec * 1000000ull + (uint64_t)t.tv_nsec / 1000ull;
	}

	return 0;
}


/** @todo -> pbuf */
/* For golay encoding/decoding. */
static uint32_t btu24(const uint8_t bytes[3]) {
	return (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
}


/** @todo -> pbuf */
/* For golay encoding/decoding. */
static void u24tb(uint32_t v, uint8_t bytes[3]) {
	bytes[0] = (v >> 16) & 0xff;
	bytes[1] = (v >> 8) & 0xff;
	bytes[2] = v & 0xff;
}


static rmac_ret_t rmac_slot_exec(Rmac *self, struct rmac_slot *slot) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(self != NULL, RMAC_RET_BAD_ARG);

	/* Configure the radio. */
	radio_set_sync(self->radio, self->radio_sync, RMAC_RADIO_SYNC_SIZE);

	/* Check for the time match. Adjust the negative delay to get exact match. */
	/** @todo */
	self->slot_start_time_error_ema_us = (self->slot_start_time_error_ema_us * 15 + (slot->slot_start_us - rmac_time(self))) / 16;

	rmac_ret_t r = RMAC_RET_OK;

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
					r = RMAC_RET_OK;
				}

				/* Packet received. If not in the low power mode, now is the
				 * right time to send packets to low power neighbors. */
				if (self->tdma_algo == RMAC_TDMA_ALGO_CSMA) {
					struct rmac_slot tx_slot = {
						.slot_start_us = rmac_time(self) + 10000ull,
						.slot_length_us = 100000ul,
						.type = RMAC_SLOT_TYPE_TX_BROADCAST,
						.peer_id = 0,
						.packet = NULL,
					};
					rmac_slot_queue_insert(&self->slot_queue, &tx_slot);
				}
			} else if (ret == IRADIO_RET_TIMEOUT) {
				r = RMAC_RET_NOOP;
			}

			break;
		}
		case RMAC_SLOT_TYPE_TX_BROADCAST:
		case RMAC_SLOT_TYPE_TX_CONTROL:
		case RMAC_SLOT_TYPE_TX_UNICAST: {
			/** @todo get packet from the slot instead. */
			struct radio_scheduler_packet *packet = slot->packet;
			if (packet == NULL) {
				r = RMAC_RET_NOOP;
			} else {
				if (radio_send(self->radio, packet->txpacket.buf, packet->txpacket.len, &packet->txpacket.txparams) == IRADIO_RET_OK) {
					/* Packet sent. For now, open a short RX slot. */
					if (r == RMAC_RET_OK && self->tdma_algo == RMAC_TDMA_ALGO_IMMEDIATE_RX) {
						struct rmac_slot rx_slot = {
							.slot_start_us = rmac_time(self),
							.slot_length_us = 20000ul,
							.type = RMAC_SLOT_TYPE_RX_UNMANAGED,
							.peer_id = 0,
							.packet = NULL,
						};
						rmac_slot_queue_insert(&self->slot_queue, &rx_slot);
					}
				} else {
					r = RMAC_RET_FAILED;
				}
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
	Rmac *self = (Rmac *)p;

	while (true) {
		struct rmac_slot slot = {0};
		if (rmac_slot_queue_peek(&self->slot_queue, &slot) != RMAC_RET_OK) {
			/* No scheduled slot in the queue. This is unusual as there should
			 * always be some slots scheduled. Wait a bit and retry. */
			vTaskDelay(10);
			continue;
		}

		uint64_t time = rmac_time(self);

		if ((slot.slot_start_us + slot.slot_length_us) <= time) {
			/* We missed the whole slot. Discard it and try to catch the
			 * next one. */
			rmac_slot_queue_remove(&self->slot_queue, &slot);
			rmac_packet_pool_release(&self->packet_pool, slot.packet);
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
			vTaskDelay(w);
			continue;
		}

		/* The slot is lesser than 2ms in the future. There is no such precise wait
		 * supported by the RTOS, busy-wait the rest instead. */
		while (true) {
			time = rmac_time(self);
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
				rmac_packet_pool_release(&self->packet_pool, slot.packet);
				continue;
			}

			/* Finally we managed to get to the beginning. Remove the slot from
			 * the queue and start the radio. Shorten the slot a bit in order
			 * to stop listening in time. We are not expecting any packet just
			 * before the slot ends. */
			/** @todo adaptive based on slot_expected_end */
			slot.slot_length_us = len_remaining - 3000;

			// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("exact"));
			/* Execute the slot. If the packet is sent, release it back to the pool. */
			rmac_slot_queue_remove(&self->slot_queue, &slot);
			rmac_slot_exec(self, &slot);
			rmac_packet_pool_release(&self->packet_pool, slot.packet);
		}

		if (self->state == RMAC_STATE_STOP_REQ) {
			break;
		}
	}

	vTaskDelete(NULL);
}


static void slot_scheduler_task(void *p) {
	Rmac *self = (Rmac *)p;

	vTaskDelay(1000);
	uint64_t next_slot = rmac_time(self);
	while (true) {

		uint64_t time = rmac_time(self);
		if (self->tdma_algo == RMAC_TDMA_ALGO_IMMEDIATE_RX) {
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
					.packet = NULL,
				};
				rmac_slot_queue_insert(&self->slot_queue, &slot);
				next_slot += 200000ull;
			}
		}
		if (self->tdma_algo == RMAC_TDMA_ALGO_CSMA) {
			/* In the full-power mode, wait for a packet. Match it to the corresponding
			 * neighbor and check if it is in the low power mode. If yes, this is
			 * the right time to respond. Get up to n packets from the neighbor
			 * queue and send them. If the neighbor is in full power mode too,
			 * basically the same thing could be done except the timing is not
			 * so strict. */

			while (rmac_slot_queue_len(&self->slot_queue) < 10 && next_slot < (time + 2000000ull)) {
				struct rmac_slot slot = {
					.slot_start_us = next_slot,
					.slot_length_us = 200000ul,
					.type = RMAC_SLOT_TYPE_RX_UNMANAGED,
					.peer_id = 0,
					.packet = NULL,
				};
				rmac_slot_queue_insert(&self->slot_queue, &slot);
				next_slot += 200000ull;
			}
		}
		if (self->tdma_algo == RMAC_TDMA_ALGO_HASH) {
			/** @todo */
		}
		vTaskDelay(100);
	}

	vTaskDelete(NULL);
}


rmac_ret_t process_packet(Rmac *self, struct radio_scheduler_rxpacket *packet) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(packet != NULL, RMAC_RET_BAD_ARG);

	#if 0
	if (false) {
		if ((packet->len % 6) != 0) {
			return RMAC_RET_FAILED;
		}
		for (size_t i = 0; i < (packet->len / 6); i++) {
			uint32_t g1 = fec_golay_correct(btu24(&buf[i * 6]));
			uint32_t g2 = fec_golay_correct(btu24(&buf[i * 6 + 3]));
			uint32_t triplet = ((g1 & 0x0fff) << 12) | (g2 & 0x0fff);
			u24tb(triplet, &(buf[i * 3]));
		}
		len /= 2;

		len = buf[0];
		for (size_t i = 0; i < len; i++) {
			buf[i] = buf[i + 1];
		}
	}
	#endif

	rmac_pbuf_clear(&self->rx_pbuf);
	rmac_pbuf_universe_key(&self->rx_pbuf, self->universe_key, self->universe_key_len);
	rmac_pbuf_ret_t ret = rmac_pbuf_read(&self->rx_pbuf, packet->buf, packet->len);
	if (ret != RMAC_PBUF_RET_OK) {
		return RMAC_RET_FAILED;
	}

	uint32_t destination = self->rx_pbuf.msg.destination;
	uint32_t source = self->rx_pbuf.msg.source;
	uint32_t context = self->rx_pbuf.msg.context;
	uint8_t counter = self->rx_pbuf.msg.counter;

	struct nbtable_item *item = nbtable_find_or_add_id(&self->nbtable, source);
	if (item == NULL) {
		/* If we cannot manage the neighbor, drop the packet. */
		return RMAC_RET_FAILED;
	}
	nbtable_update_rx_counter(&self->nbtable, item, counter, self->rx_pbuf.msg.data.size);
	nbtable_update_rssi(&self->nbtable, item, packet->rxparams.rssi_dbm / 10.0);

	if (destination == 0 || destination == self->node_id) {
		struct iradio_mac_rx_message msg = {0};
		if (self->rx_pbuf.msg.data.size == 0 || self->rx_pbuf.msg.data.size > sizeof(msg.buf)) {
			return RMAC_RET_FAILED;
		}
		memcpy(msg.buf, self->rx_pbuf.msg.data.bytes, self->rx_pbuf.msg.data.size);
		msg.len = self->rx_pbuf.msg.data.size;
		msg.source = source;

		hradio_mac_put_received_packet(&self->iface_host, &msg, context);
	} else {
		/* Not for me. */
	}

	return RMAC_RET_OK;
}


static void rx_process_task(void *p) {
	Rmac *self = (Rmac *)p;

	while (true) {
		struct radio_scheduler_rxpacket packet = {0};
		if (radio_scheduler_rx(self, &packet) == RMAC_RET_OK) {
			process_packet(self, &packet);
		}

		if (self->state == RMAC_STATE_STOP_REQ) {
			break;
		}
	}

	vTaskDelete(NULL);
}


static void tx_process_task(void *p) {
	Rmac *self = (Rmac *)p;

	while (true) {

		struct iradio_mac_tx_message msg = {0};
		if (hradio_mac_get_packet_to_send(&self->iface_host, &msg) == HRADIO_MAC_RET_OK) {

			/* Allocate the packet first. */

			struct radio_scheduler_packet *packet = rmac_packet_pool_get(&self->packet_pool);
			while (packet == NULL) {
				vTaskDelay(2);
				packet = rmac_packet_pool_get(&self->packet_pool);
			}
			/* Now check if a slot of the required type is available. */
			if (rmac_slot_queue_is_available(&self->slot_queue, RMAC_SLOT_TYPE_TX_BROADCAST) == false) {
				/* If not, wait until it is. */
				xSemaphoreTake(self->slot_queue.tx_available, 0);
				xSemaphoreTake(self->slot_queue.tx_available, portMAX_DELAY);
			}
			/* Attach the allocated packet to the slot. */
			struct rmac_slot slot;
			if (rmac_slot_queue_attach_packet(&self->slot_queue, RMAC_SLOT_TYPE_TX_BROADCAST, packet, &slot) != RMAC_RET_OK) {
				rmac_packet_pool_release(&self->packet_pool, packet);
				continue;
			}

			rmac_pbuf_clear(&self->tx_pbuf);
			rmac_pbuf_universe_key(&self->tx_pbuf, self->universe_key, self->universe_key_len);
			self->tx_pbuf.msg.source = self->node_id;
			self->tx_pbuf.msg.has_context = true;
			self->tx_pbuf.msg.context = msg.context;
			self->tx_pbuf.msg.has_destination = true;
			self->tx_pbuf.msg.destination = msg.destination;
			self->tx_pbuf.msg.has_counter = true;
			self->tx_pbuf.msg.counter = self->tx_counter;
			self->tx_pbuf.msg.has_time = true;
			self->tx_pbuf.msg.time = slot.slot_start_us;
			self->tx_counter++;

			memcpy(self->tx_pbuf.msg.data.bytes, msg.buf, msg.len);
			self->tx_pbuf.msg.data.size = msg.len;

			rmac_pbuf_ret_t ret = rmac_pbuf_write(&self->tx_pbuf, packet->txpacket.buf, 256, &packet->txpacket.len);
			if (ret != RMAC_PBUF_RET_OK) {
				continue;
			}

			if (false) {
				/* 125B is the maximum message length when using Golay encoding.
				 * Drop the packet if it is larger. */
				if (packet->txpacket.len > 125) {
					continue;
				}

				/* Save the packet length in the first byte. We have to move
				 * the rest. */
				/** @todo unoptimal */
				for (size_t i = packet->txpacket.len; i > 0; i--) {
					packet->txpacket.buf[i] = packet->txpacket.buf[i - 1];
				}
				packet->txpacket.buf[0] = packet->txpacket.len;
				packet->txpacket.len++;

				/* Pad the length to be divisible by 3. */
				while (packet->txpacket.len % 3) {
					packet->txpacket.len++;
				}

				for (int32_t i = packet->txpacket.len; i >= 0; i--) {
					uint32_t triplet = btu24(&(packet->txpacket.buf[i * 3]));
					uint32_t g1 = fec_golay_encode((triplet >> 12) & 0x0fff);
					uint32_t g2 = fec_golay_encode(triplet & 0x0fff);
					u24tb(g1, &(packet->txpacket.buf[i * 6]));
					u24tb(g2, &(packet->txpacket.buf[i * 6 + 3]));
				}
				packet->txpacket.len *= 2;
			}
		}

		if (self->state == RMAC_STATE_STOP_REQ) {
			break;
		}
	}

	vTaskDelete(NULL);
}


rmac_ret_t radio_scheduler_start(Rmac *self) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	self->radio_scheduler_rxqueue = xQueueCreate(2, sizeof(struct radio_scheduler_rxpacket));
	if(self->radio_scheduler_rxqueue == NULL) {
		return RMAC_RET_FAILED;
	}

	if (rmac_slot_queue_init(&self->slot_queue, 31) != RMAC_RET_OK) {
		return RMAC_RET_FAILED;
	}

	if (rmac_packet_pool_init(&self->packet_pool, 1) != RMAC_RET_OK) {
		return RMAC_RET_FAILED;
	}

	xTaskCreate(radio_scheduler_task, "rmac-radio-sch", configMINIMAL_STACK_SIZE + 1024, (void *)self, 3, &(self->radio_scheduler_task));
	if (self->radio_scheduler_task == NULL) {
		return RMAC_RET_FAILED;
	}

	xTaskCreate(slot_scheduler_task, "rmac-radio-ssch", configMINIMAL_STACK_SIZE + 1024, (void *)self, 1, &(self->slot_scheduler_task));
	if (self->radio_scheduler_task == NULL) {
		return RMAC_RET_FAILED;
	}

	xTaskCreate(rx_process_task, "rmac-rxprocess", configMINIMAL_STACK_SIZE + 1024, (void *)self, 1, &(self->rx_process_task));
	if (self->rx_process_task == NULL) {
		return RMAC_RET_FAILED;
	}
	xTaskCreate(tx_process_task, "rmac-txprocess", configMINIMAL_STACK_SIZE + 1024, (void *)self, 1, &(self->tx_process_task));
	if (self->tx_process_task == NULL) {
		return RMAC_RET_FAILED;
	}

	return RMAC_RET_OK;
}


rmac_ret_t radio_scheduler_rx(Rmac *self, struct radio_scheduler_rxpacket *packet) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(packet != NULL, RMAC_RET_BAD_ARG);

	if (xQueueReceive(self->radio_scheduler_rxqueue, packet, portMAX_DELAY) != pdTRUE) {
		return RMAC_RET_FAILED;
	}

	return RMAC_RET_OK;
}


rmac_ret_t rmac_slot_queue_init(struct rmac_slot_queue *self, size_t size) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	memset(self, 0, sizeof(struct rmac_slot_queue));
	self->items = calloc(size, sizeof(struct rmac_slot));
	if (self->items == NULL) {
		return RMAC_RET_FAILED;
	}
	self->size = size;

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return RMAC_RET_FAILED;
	}

	self->tx_available = xSemaphoreCreateBinary();
	if (self->tx_available == NULL) {
		return RMAC_RET_FAILED;
	}

	return RMAC_RET_OK;
}


rmac_ret_t rmac_slot_queue_free(struct rmac_slot_queue *self) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	free(self->items);

	return RMAC_RET_OK;
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


rmac_ret_t rmac_slot_queue_insert(struct rmac_slot_queue *self, struct rmac_slot *slot) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(slot != NULL, RMAC_RET_BAD_ARG);

	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("insert s=%u l=%u ql=%u"), (uint32_t)slot->slot_start_us, slot->slot_length_us, self->len);

	/* Force some fields. */
	slot->packet = NULL;

	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (self->len >= self->size) {
		/* Queue full. */
		xSemaphoreGive(self->lock);
		return RMAC_RET_FAILED;
	}

	memcpy(&(self->items[self->len]), slot, sizeof(struct rmac_slot));
	self->len++;
	rmac_slot_queue_siftup(self, self->len - 1);
	xSemaphoreGive(self->lock);
	if (slot->type == RMAC_SLOT_TYPE_TX_BROADCAST) {
		xSemaphoreGive(self->tx_available);
	}

	return RMAC_RET_OK;
}


rmac_ret_t rmac_slot_queue_peek(struct rmac_slot_queue *self, struct rmac_slot *slot) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(slot != NULL, RMAC_RET_BAD_ARG);

	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (self->len == 0) {
		xSemaphoreGive(self->lock);
		return RMAC_RET_FAILED;
	}

	memcpy(slot, &(self->items[0]), sizeof(struct rmac_slot));
	xSemaphoreGive(self->lock);
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("peek s=%u l=%u"), (uint32_t)slot->slot_start_us, slot->slot_length_us);

	return RMAC_RET_OK;
}


rmac_ret_t rmac_slot_queue_remove(struct rmac_slot_queue *self, struct rmac_slot *slot) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(slot != NULL, RMAC_RET_BAD_ARG);

	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (self->len == 0) {
		xSemaphoreGive(self->lock);
		return RMAC_RET_FAILED;
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
	return RMAC_RET_OK;
}


size_t rmac_slot_queue_len(struct rmac_slot_queue *self) {
	ASSERT(self != NULL, 0);

	return self->len;
}


bool rmac_slot_queue_is_available(struct rmac_slot_queue *self, enum rmac_slot_type type) {
	ASSERT(self != NULL, false);

	for (size_t i = 0; i < self->len; i++) {
		if (self->items[i].type == type) {
			return true;
		}
	}
	return false;
}


rmac_ret_t rmac_slot_queue_attach_packet(struct rmac_slot_queue *self, enum rmac_slot_type type, struct radio_scheduler_packet *packet, struct rmac_slot *slot) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(slot != NULL, RMAC_RET_BAD_ARG);

	xSemaphoreTake(self->lock, portMAX_DELAY);
	for (size_t i = 0; i < self->len; i++) {
		if (self->items[i].packet == NULL && self->items[i].type == type) {
			self->items[i].packet = packet;
			memcpy(slot, &(self->items[i]), sizeof(struct rmac_slot));
			xSemaphoreGive(self->lock);
			return RMAC_RET_OK;
		}
	}

	/* No scheduled slot in the queue is free. */
	xSemaphoreGive(self->lock);
	return RMAC_RET_FAILED;
}


/* Packet pool implementation */

rmac_ret_t rmac_packet_pool_init(RmacPacketPool *self, size_t size) {
	ASSERT(self != NULL, RMAC_RET_NULL);
	ASSERT(size > 0, RMAC_RET_BAD_ARG);

	memset(self, 0, sizeof(RmacPacketPool));
	self->size = size;
	self->pool = calloc(size, sizeof(struct radio_scheduler_packet));
	if (self->pool == NULL) {
		return RMAC_RET_FAILED;
	}
	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return RMAC_RET_FAILED;
	}

	return RMAC_RET_OK;
}


rmac_ret_t rmac_packet_pool_free(RmacPacketPool *self) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	free(self->pool);
	self->pool = NULL;
	self->size = 0;

	return RMAC_RET_OK;
}


struct radio_scheduler_packet *rmac_packet_pool_get(RmacPacketPool *self) {
	ASSERT(self != NULL, NULL);

	xSemaphoreTake(self->lock, portMAX_DELAY);
	for (size_t i = 0; i < self->size; i++) {
		if (self->pool[i].used == false) {
			self->pool[i].used = true;
			xSemaphoreGive(self->lock);
			return &(self->pool[i]);
		}
	}
	xSemaphoreGive(self->lock);
	return NULL;
}


rmac_ret_t rmac_packet_pool_release(RmacPacketPool *self, struct radio_scheduler_packet *packet) {
	ASSERT(self != NULL, RMAC_RET_NULL);

	if (packet != NULL) {
		xSemaphoreTake(self->lock, portMAX_DELAY);
		ASSERT(packet->used == true, RMAC_RET_FAILED);
		packet->used = false;
		xSemaphoreGive(self->lock);
	}

	return RMAC_RET_OK;
}





