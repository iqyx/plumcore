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
#include "radio-mac-simple.h"
#include "interfaces/radio-mac/host.h"
#include "crc.h"
#include "fec_golay.h"

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


static mac_simple_ret_t nbtable_init(NbTable *self, size_t items) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);

	memset(self, 0, sizeof(NbTable));

	self->items = malloc(sizeof(struct nbtable_item) * items);
	if (self->items == NULL) {
		return MAC_SIMPLE_RET_FAILED;
	}
	self->count = items;
	for (size_t i = 0; i < items; i++) {
		self->items[i].used = false;
	}

	return MAC_SIMPLE_RET_OK;
}


static mac_simple_ret_t nbtable_free(NbTable *self) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);

	free(self->items);
	self->count = 0;

	return MAC_SIMPLE_RET_OK;
}


static struct nbtable_item *nbtable_find_id(NbTable *self, uint32_t id) {
	for (size_t i = 0; i < self->count; i++) {
		if (self->items[i].used && self->items[i].id == id) {
			return &(self->items[i]);
		}
	}
	return NULL;
}


static struct nbtable_item *nbtable_find_empty(NbTable *self) {
	for (size_t i = 0; i < self->count; i++) {
		if (self->items[i].used ==false) {
			return &(self->items[i]);
		}
	}
	return NULL;
}


static mac_simple_ret_t nbtable_add_nb(NbTable *self, uint32_t id, float rssi_dbm, uint8_t counter, size_t len) {

	/* Find the corresponding neighbor table item. Try to create a new one
	 * if not found. */
	struct nbtable_item *item = nbtable_find_id(self, id);
	if (item == NULL) {
		item = nbtable_find_empty(self);
		if (item == NULL) {
			return MAC_SIMPLE_RET_FAILED;
		}
		memset(item, 0, sizeof(struct nbtable_item));
		item->used = true;
		item->id = id;
		item->counter = counter;
	}

	/* Exponential moving average of the packet RSSI. */
	item->rssi_dbm = (15 * item->rssi_dbm + rssi_dbm) / 16.0;

	/* Check if the packet counter was incremented by 1. Compute
	 * number of missed packets. */
	int8_t counter_diff = counter - item->counter;
	if (counter_diff > 0) {
		item->rxmissed += counter_diff - 1;
		item->counter = counter;
	}
	// if (counter_diff > 1 && self->debug) {
		// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("miss rxp=%d rxm=%d"),
		      // item->rxpackets, item->rxmissed);
	// }

	/* Update neighbor statistics and reset the item age value. */
	item->rxpackets++;
	item->rxbytes += len;
	item->time = 0;

	// if (self->debug) {
		// u_log(system_log, LOG_TYPE_DEBUG,
		      // U_LOG_MODULE_PREFIX("NB id=%08x rssi=%d counter=%d rxp=%d rxb=%d rxm=%d"),
		      // item->id, (int32_t)item->rssi_dbm, (int32_t)item->counter,
		      // item->rxpackets, item->rxbytes, item->rxmissed);
	// }

	return MAC_SIMPLE_RET_OK;
}


static mac_simple_ret_t process_packet(MacSimple *self, uint8_t *buf, size_t len, int32_t rssi_dbm) {
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
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("crc check failed"));
		}
		return MAC_SIMPLE_RET_FAILED;
	}

	nbtable_add_nb(&self->nbtable, source, rssi_dbm / 10.0, counter, data_len);

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


static mac_simple_ret_t receive_packet(MacSimple *self) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);

	/* Prepare the receiver. */
	radio_set_sync(self->radio, self->network_id, MAC_SIMPLE_NETWORK_ID_SIZE);

	/* Wait for a packet for a specified time. */
	/** @todo adjust the listening time */

	/* Buffers for received packet. */
	uint8_t buf[256];
	size_t len = 0;
	struct iradio_receive_params rxparams = {0};

	iradio_ret_t ret = IRADIO_RET_FAILED;

	if (self->low_power) {
		ret = radio_receive(self->radio, buf, sizeof(buf), &len, &rxparams, 20000);
	} else {
		ret = radio_receive(self->radio, buf, sizeof(buf), &len, &rxparams, 200000);
	}

	if (ret == IRADIO_RET_OK) {
		// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("rx packet len=%d rssi=%d"), len, rxparams.rssi_dbm);
		if (process_packet(self, buf, len, rxparams.rssi_dbm) == MAC_SIMPLE_RET_OK) {
			return MAC_SIMPLE_RET_OK;
		}
	} else if (ret == IRADIO_RET_TIMEOUT) {
		return MAC_SIMPLE_RET_TIMEOUT;
	}

	return MAC_SIMPLE_RET_FAILED;
}


static mac_simple_ret_t transmit_packet(MacSimple *self) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);

	struct iradio_mac_tx_message msg = {0};
	struct iradio_send_params params = {0};
	if (hradio_mac_get_packet_to_send(&self->iface_host, &msg) == HRADIO_MAC_RET_OK) {

		/* 126B is the maximum message length when using Golay encoding. */
		if (msg.len > (126 - 16)) {
			return MAC_SIMPLE_RET_FAILED;
		}

		uint8_t buf[256];
		size_t len = 0;

		u32tb(msg.destination, buf);
		u32tb(self->node_id, buf + 4);
		u32tb(msg.context, buf + 8);
		buf[12] = self->tx_counter;
		self->tx_counter++;
		buf[13] = msg.len;
		memcpy(buf + 14, msg.buf, msg.len);
		uint16_t crc_computed = crc16(buf, 14 + msg.len);
		u16tb(crc_computed, buf + 14 + msg.len);
		len = msg.len + 16;

		/* Pad the length to be divisible by 3. */
		while (len % 3) {
			len++;
		}

		for (int32_t i = len; i >= 0; i--) {
			uint32_t triplet = btu24(&(buf[i * 3]));
			uint32_t g1 = fec_golay_encode((triplet >> 12) & 0x0fff);
			uint32_t g2 = fec_golay_encode(triplet & 0x0fff);
			u24tb(g1, &(buf[i * 6]));
			u24tb(g2, &(buf[i * 6 + 3]));
		}
		len *= 2;

		radio_send(self->radio, buf, len, &params);
		return MAC_SIMPLE_RET_OK;
	}

	return MAC_SIMPLE_RET_FAILED;
}


static void mac_task(void *p) {
	MacSimple *self = (MacSimple *)p;

	while (true) {

		if (self->low_power) {
			/* In the low power mode, we do not want to receive all the time. Instead
			 * the transmit queue is checked occasionally and packets are transmitted
			 * until the queue is empty. Then a reception window is opened for a
			 * specified time. If a packet is received, it is lengthened until no more
			 * packets are received. Radio is then turned off to preserve power. */
			while (transmit_packet(self) == MAC_SIMPLE_RET_OK) {
				while (receive_packet(self) != MAC_SIMPLE_RET_TIMEOUT) {
					;
				}
			}

			/** @todo this time should be configurable */
			vTaskDelay(200);
		} else {
			/* In the full-power mode, wait for a packet. Match it to the corresponding
			 * neighbor and check if it is in the low power mode. If yes, this is
			 * the right time to respond. Get up to n packets from the neighbor
			 * queue and send them. If the neighbor is in full power mode too,
			 * basically the same thing could be done except the timing is not
			 * so strict. */
			if (receive_packet(self) != MAC_SIMPLE_RET_TIMEOUT) {
				while (transmit_packet(self) == MAC_SIMPLE_RET_OK) {
					;
				}
			}
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

	if (nbtable_init(&self->nbtable, 16) != MAC_SIMPLE_RET_OK) {
		goto err;
	}

	fec_golay_table_fill();
	self->low_power = true;
	self->debug = false;

	self->state = MAC_SIMPLE_STATE_RUNNING;
	xTaskCreate(mac_task, "mac-simple", configMINIMAL_STACK_SIZE + 1024, (void *)self, 2, &(self->task));
	if (self->task == NULL) {
		goto err;
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

	return MAC_SIMPLE_RET_OK;
}


mac_simple_ret_t mac_simple_set_mcs(MacSimple *self, struct mac_simple_mcs *mcs) {
	ASSERT(self != NULL, MAC_SIMPLE_RET_NULL);
	CHECK(mcs != MAC_SIMPLE_MCS_NONE, MAC_SIMPLE_RET_BAD_ARG);

	self->mcs = mcs;

	return MAC_SIMPLE_RET_OK;
}




