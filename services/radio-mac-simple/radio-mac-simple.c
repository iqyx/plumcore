/*
 * A simple (CSMA) radio MAC
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

#include "u_log.h"

#include "interfaces/radio.h"
#include "interfaces/radio-mac/host.h"
#include "interfaces/clock/descriptor.h"
#include "crc.h"
#include "fec_golay.h"

#include "radio-mac-simple.h"
#include "radio-scheduler.h"
#include "nb-table.h"

#define MODULE_NAME "mac-simple"

/**
 * @todo
 *   - move message parsing and building helper functions away
 *   - allow lowpower switch
 *   - enter lowpower strategy (if at least one full-power device is in the range)
 *   - adaptive packet sending in low power mode based on the queue length
 *   - per-neighbor transmit scheduler
 *   - insert NOP packets when there is no traffic
 *   - configurable ARQ
 */


/* Protobuf-style encoding/decoding. */

static enum mac_simple_header msg_check_header(const uint8_t *buf) {
	return *buf >> 5;
}


static mac_simple_ret_t msg_get_uint32(uint8_t **buf, size_t len, uint32_t *v) {
	/* We are starting at the header. Extract the value type. */
	enum mac_simple_vtype vtype = (**buf) & 0x07;
	if (vtype != MAC_SIMPLE_VTYPE_VARINT) {
		return MAC_SIMPLE_RET_FAILED;
	}
}


static uint32_t btu32(const uint8_t bytes[4]) {
	return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
}


static uint32_t btu24(const uint8_t bytes[3]) {
	return (bytes[0] << 16) | (bytes[1] << 8) | bytes[2];
}


static uint16_t btu16(const uint8_t bytes[2]) {
	return (bytes[0] << 8) | bytes[1];
}


static void u32tb(uint32_t v, uint8_t bytes[4]) {
	bytes[0] = v >> 24;
	bytes[1] = (v >> 16) & 0xff;
	bytes[2] = (v >> 8) & 0xff;
	bytes[3] = v & 0xff;
}


static void u24tb(uint32_t v, uint8_t bytes[3]) {
	bytes[0] = (v >> 16) & 0xff;
	bytes[1] = (v >> 8) & 0xff;
	bytes[2] = v & 0xff;
}


static void u16tb(uint16_t v, uint8_t bytes[2]) {
	bytes[0] = (v >> 8) & 0xff;
	bytes[1] = v & 0xff;
}


static void debug_log_parsed_packet(uint32_t destination, uint32_t source, uint32_t context, uint8_t counter, const uint8_t *buf, size_t len, uint16_t crc) {
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("packet dst=%d src=%d ctx=%d counter=%d data_len=%d crc=%04x"), destination, source, context, counter, len, crc);
}


static void debug_log_buf(uint8_t *buf, size_t len) {
	const char *a = "0123456789abcdef";
	char s[len * 3 + 2];
	memset(s, 0, len * 3 + 2);
	for (size_t i = 0; i < len; i++) {
		s[i * 3] = a[buf[i] >> 4];
		s[i * 3 + 1] = a[buf[i] & 0x0f];
		s[i * 3 + 2] = ' ';
	}
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("buf %s"), s);
}


mac_simple_ret_t process_packet(MacSimple *self, uint8_t *buf, size_t len, int32_t rssi_dbm) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(buf != NULL, MAC_SIMPLE_RET_BAD_ARG);

	/* Total length is less than the minimum header length. */
	/** @todo remove */
	if (len < 32) {
		// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("rx packet too short"));
		return MAC_SIMPLE_RET_FAILED;
	}


	if (self->mcs->fec == MAC_SIMPLE_FEC_TYPE_GOLAY1224) {
		/* Golay decode. */
		if ((len % 6) != 0) {
			// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("not divisible by 6"));
			return MAC_SIMPLE_RET_FAILED;
		}
		for (size_t i = 0; i < (len / 6); i++) {
			uint32_t g1 = fec_golay_correct(btu24(&buf[i * 6]));
			uint32_t g2 = fec_golay_correct(btu24(&buf[i * 6 + 3]));
			uint32_t triplet = ((g1 & 0x0fff) << 12) | (g2 & 0x0fff);
			u24tb(triplet, &(buf[i * 3]));
		}
		len /= 2;
	}

	/* There is a 14B fixed-size header on the beginning for now. */
	uint32_t destination = btu32(buf);
	uint32_t source = btu32(buf + 4);
	uint32_t context = btu32(buf + 8);
	uint8_t counter = buf[12];
	uint8_t data_len = buf[13];

	/* Check if the data_len fits inside the received buffer
	 * (exclude 2 byte CRC and 14 byte header). */
	if (data_len > (len - 16)) {
		if (self->debug) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("bad data_len = %d"), data_len);
		}
		return MAC_SIMPLE_RET_FAILED;
	}
	uint16_t crc_received = btu16(buf + 14 + data_len);
	uint16_t crc_computed = crc16(buf, 14 + data_len);

	if (self->debug) {
		debug_log_parsed_packet(destination, source, context, counter, buf + 14, data_len, crc_received);
	}

	if (crc_received != crc_computed) {
		if (self->debug) {
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("crc check failed %04x != %04x"), crc_received, crc_computed);
		}
		return MAC_SIMPLE_RET_FAILED;
	}

	struct nbtable_item *item = nbtable_find_or_add_id(&self->nbtable, source);
	if (item == NULL) {
		/* If we cannot manage the neighbor, drop the packet. */
		return MAC_SIMPLE_RET_FAILED;
	}
	nbtable_update_rx_counter(&self->nbtable, item, counter, data_len);
	nbtable_update_rssi(&self->nbtable, item, rssi_dbm / 10.0);

	if (destination == 0 || destination == self->node_id) {
		struct iradio_mac_rx_message msg = {0};
		if (len == 0 || len > sizeof(msg.buf)) {
			return MAC_SIMPLE_RET_FAILED;
		}
		memcpy(msg.buf, buf + 14, data_len);
		msg.len = data_len;
		msg.source = source;

		hradio_mac_put_received_packet(&self->iface_host, &msg, context);
	} else {
		// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("not for me"));
	}
	return MAC_SIMPLE_RET_OK;
}


static void rx_process_task(void *p) {
	MacSimple *self = (MacSimple *)p;

	while (true) {
		struct radio_scheduler_rxpacket packet = {0};
		if (radio_scheduler_rx(self, &packet) == MAC_SIMPLE_RET_OK) {
			process_packet(self, packet.buf, packet.len, packet.rxparams.rssi_dbm);
		}

		if (self->state == MAC_SIMPLE_STATE_STOP_REQ) {
			break;
		}
	}

	vTaskDelete(NULL);
}


static void tx_process_task(void *p) {
	MacSimple *self = (MacSimple *)p;

	while (true) {

		struct iradio_mac_tx_message msg = {0};
		if (hradio_mac_get_packet_to_send(&self->iface_host, &msg) == HRADIO_MAC_RET_OK) {

			struct radio_scheduler_txpacket packet = {0};

			u32tb(msg.destination, packet.buf);
			u32tb(self->node_id, &(packet.buf[4]));
			u32tb(msg.context, &packet.buf[8]);
			packet.buf[12] = self->tx_counter;
			self->tx_counter++;
			packet.buf[13] = msg.len;
			memcpy(&(packet.buf[14]), msg.buf, msg.len);
			uint16_t crc_computed = crc16(packet.buf, 14 + msg.len);
			u16tb(crc_computed, &(packet.buf[14 + msg.len]));
			packet.len = msg.len + 16;

			if (self->mcs->fec == MAC_SIMPLE_FEC_TYPE_GOLAY1224) {
				/* 126B is the maximum message length when using Golay encoding.
				 * Drop the packet if it is larger. */
				if (msg.len > 126) {
					continue;
				}

				/* Pad the length to be divisible by 3. */
				while (packet.len % 3) {
					packet.len++;
				}

				for (int32_t i = packet.len; i >= 0; i--) {
					uint32_t triplet = btu24(&(packet.buf[i * 3]));
					uint32_t g1 = fec_golay_encode((triplet >> 12) & 0x0fff);
					uint32_t g2 = fec_golay_encode(triplet & 0x0fff);
					u24tb(g1, &(packet.buf[i * 6]));
					u24tb(g2, &(packet.buf[i * 6 + 3]));
				}
				packet.len *= 2;
			}

			radio_scheduler_tx(self, &packet);
		}

		if (self->state == MAC_SIMPLE_STATE_STOP_REQ) {
			break;
		}
	}

	vTaskDelete(NULL);
}



mac_simple_ret_t mac_simple_init(MacSimple *self, Radio *radio) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(radio != NULL, MAC_SIMPLE_RET_BAD_ARG);

	memset(self, 0, sizeof(MacSimple));
	self->radio = radio;

	if (iradio_mac_init(&self->iface) != IRADIO_MAC_RET_OK) {
		goto err;
	}

	if (hradio_mac_connect(&self->iface_host, &self->iface)) {
		goto err;
	}

	mac_simple_set_network_name(self, "default");
	mac_simple_set_mcs(self, 0);

	if (nbtable_init(&self->nbtable, 16) != NBTABLE_RET_OK) {
		goto err;
	}

	fec_golay_table_fill();
	self->low_power = false;
	self->debug = false;

	if (radio_scheduler_start(self) != MAC_SIMPLE_RET_OK) {
		goto err;
	}

	xTaskCreate(rx_process_task, "rmac-rxprocess", configMINIMAL_STACK_SIZE + 512, (void *)self, 1, &(self->rx_process_task));
	if (self->rx_process_task == NULL) {
		return MAC_SIMPLE_RET_FAILED;
	}
	xTaskCreate(tx_process_task, "rmac-txprocess", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->tx_process_task));
	if (self->tx_process_task == NULL) {
		return MAC_SIMPLE_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("service started"));
	return MAC_SIMPLE_RET_OK;;

err:
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("cannot start the service"));
	return MAC_SIMPLE_RET_FAILED;;
}


mac_simple_ret_t mac_simple_free(MacSimple *self) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);

	nbtable_free(&self->nbtable);

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t mac_simple_set_network_name(MacSimple *self, const char *network) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(network != NULL, MAC_SIMPLE_RET_BAD_ARG);

	/** @todo convert network name to the network_id */
	memcpy(self->network_id, "abcd", 4);
	radio_set_sync(self->radio, self->network_id, MAC_SIMPLE_NETWORK_ID_SIZE);

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t mac_simple_set_mcs(MacSimple *self, struct mac_simple_mcs *mcs) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	CHECK(mcs != MAC_SIMPLE_MCS_NONE, MAC_SIMPLE_RET_BAD_ARG);

	self->mcs = mcs;

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t mac_simple_set_clock(MacSimple *self, IClock *clock) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	ASSERT(clock != NULL, MAC_SIMPLE_RET_BAD_ARG);

	self->clock = clock;

	return MAC_SIMPLE_RET_OK;
}


uint64_t mac_simple_time(MacSimple *self) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);

	if (self->clock) {
		struct timespec t;
		iclock_get(self->clock, &t);
		return (uint64_t)t.tv_sec * 1000000ull + (uint64_t)t.tv_nsec / 1000ull;
	}

	return 0;
}

