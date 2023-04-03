/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libopencm3/stm32/fdcan.h>
#include <main.h>
#include <blake2.h>
#include <interfaces/can.h>
#include "nbus.h"
#include "blake2s-siv.h"

#define MODULE_NAME "nbus"

/**
 * @brief NBUS tl;dr
 *
 * NBUS uses a CAN2.0b or CAN-FD frames with 29 bit identifiers arranged as:
 * ...m cccc cccc cccc cccc Rrrr oooo oooo
 *
 * - m - multicast is considered low priority, this bit is the first in the ID,
 *       set to 1 to publish a multicast message
 * - c - 16 bit channel identifier
 * - R - response if R = 1
 * - r - reserved for future use
 * - o - opcode, these bits are the last, all opcodes within a channel are equal priority
 *
 * Specific statically allocated channel numbers:
 * - channel 0x0 is reserved for future use
 * - channel 0xffff is allocated for the NBUS discovery protocol
 *
 * Opcodes:
 * 0x00-0x3f - leading fragment, endpoints 0-63, 0 is the descriptor endpoint
 *             counter (4 bytes)
 *             frame length (2 bytes)
 *             flags (2 bytes)
 * 0x40-0xbf - data fragment
 * 0xc0      - blake2s-SIV trailing fragment
 *             MAC (8, 16 or 32 bytes allowed)
 *
 * @todo
 *
 * - change the way descriptors are read. Transform EP0 to a C&C EP with functions
 *   to set or get various parameters, single or multiple. Use CBOR, ideally.
 * - introduce a TFTP protocol equivalent
 * - introduce a bootloader configuration protocol
 */


const uint8_t *siphash_key[16] = {0};

/**
 * @brief Handler for packets destined for the discovery protocol (channel 0)
 *
 * Protocol details are described in the corresponding protocol documentation.
 * The protocol is purely request-response, there are no unsolicited messages.
 * It requires no background processing nor timeouts. All responses can be
 * constructed and sent in real-time.
 *
 * @return NBUS_RET_OK of the packet was processed without any error,
 *         NBUS_RET_VOID of no processing was needed or
 *         NBUS_RET_FAILED when an error occured.
 */
static nbus_ret_t nbus_discovery_rx_handler(void *context, nbus_endpoint_t ep, void *buf, size_t len) {
	Nbus *self = (Nbus *)context;

	if (ep == NBUS_DISC_STREAM_MASK_PROBE && len == sizeof(struct nbus_discovery_mask_probe_msg)) {
		struct nbus_discovery_mask_probe_msg *msg = buf;
		NbusChannel *ch = self->first;
		while (ch) {
			if (ch->inhibit_discovery == false) {
				if ((msg->id & msg->mask) == (ch->short_id & msg->mask)) {
					uint8_t confirm[1] = {0x55};
					return nbus_channel_send(&self->discovery_channel, ep, confirm, sizeof(confirm));
				}
			}
			ch = ch->next;
		}
		/* Nothing in the range */
		return NBUS_RET_VOID;
	}

	if (ep == NBUS_DISC_STREAM_SET_CHANNEL && len == sizeof(struct nbus_discovery_set_channel_msg)) {
		struct nbus_discovery_set_channel_msg *msg = buf;

		/* Find the matching channel */
		NbusChannel *ch = self->first;
		while (ch) {
			if (ch->short_id == msg->id) {
				/* And reassign its channel ID. Do not go further. */
				ch->channel = msg->channel;
				ch->channel_assigned = true;
				return NBUS_RET_OK;
			}
			ch = ch->next;
		}

		/* No channel was matched by short ID. This is somewhat a valid case, not an error. */
		return NBUS_RET_VOID;
	}

	if (ep == NBUS_DISC_STREAM_PROBE && len == sizeof(struct nbus_discovery_probe_msg)) {
		struct nbus_discovery_probe_msg *msg = buf;

		NbusChannel *ch = self->first;
		while (ch) {
			if (ch->short_id == msg->id) {
				break;
			}
			ch = ch->next;
		}
		if (ch != NULL) {
			struct nbus_discovery_probe_response_msg resp = {
				.counter = ch->counter,
				.channel = ch->channel_assigned ? ch->channel : 0
			};
			ch->counter++;
			return nbus_channel_send(&self->discovery_channel, ep, &resp, sizeof(resp));
		}

		/* No channel was matched. */
		return NBUS_RET_VOID;
	}

	return NBUS_RET_FAILED;
}


nbus_ret_t nbus_add_channel(Nbus *self, NbusChannel *channel) {
	if (channel == NULL) {
		return NBUS_RET_BAD_PARAM;
	}
	channel->next = self->first;
	channel->parent = self;
	self->first = channel;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("add channel sid=0x%08x"), channel->short_id);

	return NBUS_RET_OK;
}


static nbus_ret_t nbus_channel_rx_descriptor(NbusChannel *self, void *buf, size_t len) {
	if (len != 1) {
		return NBUS_RET_FAILED;
	}
	uint32_t line = ((uint8_t *)buf)[0];

	if (self->descriptor == NULL) {
		return NBUS_RET_FAILED;
	}

	uint8_t s[NBUS_DESCRIPTOR_LEN] = {0};
	size_t s_len = 0;
	nbus_ret_t ret = self->descriptor(self->descriptor_context, line, s, &s_len);
	if (ret == NBUS_RET_OK) {
		nbus_channel_send(self, 0, s, s_len);
	}

	return NBUS_RET_VOID;
}


static nbus_ret_t nbus_channel_receive(NbusChannel *self, struct nbus_id *id, void *buf, size_t len) {
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("ch[%u] mcast=%u resp=%u opcode=0x%02x"), id->channel, id->multicast, id->response, id->opcode);

	/* Drop all multicast packets for unicast channels and vice versa. */
	if (self->multicast != id->multicast) {
		return NBUS_RET_FAILED;
	}

	/* A peripheral device drops all responses, an initiator device drops all requests. */
	if (self->peripheral == id->response) {
		return NBUS_RET_FAILED;
	}

	switch (id->opcode) {
		case NBUS_OP_LEADING_MIN ... NBUS_OP_LEADING_MAX: {
			/* Leading frame is always dominant wrt. packet processing. Whatever the actual state is,
			 * a leading frame always causes its reset. Consider generating a warning if a different
			 * endpoint packet is currently being assembled as this means the endpoint data is
			 * interleaved (forbidden condition). */
			nbus_endpoint_t ep = id->opcode - NBUS_OP_LEADING_MIN;
			struct nbus_leading_frame_msg *msg = buf;

			if (self->in_progress) {
				/* Placeholder, do nothing here. Reset the packet. */
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("packet reassembly aborted channel %u, ep %u"), self->channel, self->ep);
			}

			/* Do not accept packets longer thatn the allowed MTU. Do not even bother receiving
			 * the rest of the packet (do not init). */
			if (msg->len > NBUS_CHANNEL_MTU) {
				u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("packet len > MTU (%u)"), msg->len);
				return NBUS_RET_FAILED;
			}

			if (self->peripheral) {
				/* In the peripheral mode it is hard to decide which packets to accept
				 * (the peripheral could have been reset in the middle).
				 * Accept all so far. */
				self->packet_counter = msg->counter;
			} else {
				/* We are in the initiator mode. Packet counter must match the previous value
				 * stored in the packet_counter value. The pacet is invalid if there is no match. */
				if (msg->counter != self->packet_counter) {
					return NBUS_RET_FAILED;
				}
			}

			self->in_progress = true;
			self->buf_len = 0;
			self->packet_size = msg->len;
			self->frame_expected = 0;
			self->ep = ep;

			// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("new packet channel %u, ep %u, size %u"), self->channel, self->ep, self->packet_size);
			/* Ignore msg->flags for now. */
			break;
		}

		case NBUS_OP_DATA_MIN ... NBUS_OP_DATA_MAX: {
			uint32_t frame = id->opcode - NBUS_OP_DATA_MIN;

			/* Do not process any packet data without leading frame received beforehand (setting
			 * in_progress to true). Also, discard any out of order frames. */
			if (self->in_progress == false || self->frame_expected != frame) {
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("enexpected frame %u, channel %u, ep %u"), frame, self->channel, self->ep);
				return NBUS_RET_FAILED;
			}
			/* Do not receive more data than is the maximum packet size (checked during the leading
			 * frame processing). */
			if ((self->buf_len + len) > self->packet_size) {
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("dropping redundant data frame %u, channel %u, ep %u"), frame, self->channel, self->ep);
				return NBUS_RET_FAILED;
			}

			memcpy(self->buf + self->buf_len, buf, len);
			self->buf_len += len;
			self->frame_expected++;
			break;
		}

		case NBUS_OP_TRAILING: {
			/* Total number of bytes received so far is different than the advertised packet length. */
			if (self->buf_len != self->packet_size) {
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("wrong packet size %u, expected %u"), self->buf_len, self->packet_size);
				self->in_progress = false;
				return NBUS_RET_FAILED;
			}

			/** @todo check the MAC here */

			if (self->rx_handler != NULL) {
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("calling packet handler channel %u, ep %u, size %u"), self->channel, self->ep, self->packet_size);
				self->rx_handler(self->rx_handler_context, self->ep, self->buf, self->buf_len);
			}

			/* Process and return the descriptor if endpoint == 0 */
			if (self->ep == 0 && self->multicast == false && id->response == false) {
				return nbus_channel_rx_descriptor(self, self->buf, self->buf_len);
			}

			self->in_progress = false;
			self->packet_size = 0;
			self->buf_len = 0;
			break;
		}

		default:
			return NBUS_RET_FAILED;
	}

	return NBUS_RET_FAILED;
}



static NbusChannel *nbus_channel_find_by_id(Nbus *self, nbus_channel_id_t id) {
	NbusChannel *ch = self->first;
	NbusChannel *found = NULL;
	while (ch) {
		if (ch->channel_assigned && ch->channel == id) {
			found = ch;
			break;
		}
		ch = ch->next;
	}
	return found;
}


static void nbus_parse_id(uint32_t id, struct nbus_id *sid) {
	sid->channel = (id >> 12) & 0xffff;
	sid->opcode = id & 0xff;
	sid->multicast = (id & (1 << 28)) != 0;
	sid->response = (id & (1 << 11)) != 0;
}


static uint32_t nbus_build_id(struct nbus_id *id) {
	uint32_t r = 0U;
	if (id->multicast) {
		r += (1U << 28);
	}
	if (id->response) {
		r += (1U << 11);
	}
	r += id->channel << 12;
	r += id->opcode;
	return r;
}


static nbus_ret_t nbus_receive(Nbus *self) {
	struct can_message msg = {0};

	if (self->can == NULL || self->can->vmt->receive == NULL) {
		return NBUS_RET_FAILED;
	}

	/** @todo adjust the timeout */
	if (self->can->vmt->receive(self->can, &msg, 10000) != CAN_RET_OK) {
		return NBUS_RET_FAILED;
	}

	if (msg.extid == false) {
		return NBUS_RET_FAILED;
	}

	struct nbus_id id;
	nbus_parse_id(msg.id, &id);

	NbusChannel *found = nbus_channel_find_by_id(self, id.channel);
	if (found == NULL) {
		return NBUS_RET_VOID;
	}

	return nbus_channel_receive(found, &id, msg.buf, msg.len);
}


static void nbus_receive_task(void *p) {
	Nbus *self = (Nbus *)p;

	while (true) {
		nbus_receive(self);
	}
	vTaskDelete(NULL);
}

nbus_ret_t nbus_channel_init(NbusChannel *self, const char *name) {
	memset(self, 0, sizeof(NbusChannel));
	self->name = name;

	return NBUS_RET_OK;
}


static nbus_ret_t nbus_channel_send_frame(NbusChannel *self, struct nbus_id *id, void *buf, size_t len) {
	Nbus *nbus = self->parent;

	struct can_message msg = {0};
	msg.id = nbus_build_id(id);
	msg.extid = true;
	memcpy(&msg.buf, buf, len);
	msg.len = len;

	if (nbus->can == NULL || nbus->can->vmt->send == NULL) {
		return NBUS_RET_FAILED;
	}

	/** @todo adjust the timeout */
	if (nbus->can->vmt->send(nbus->can, &msg, 1000) != CAN_RET_OK) {
		return NBUS_RET_FAILED;
	}

	return NBUS_RET_OK;
}


nbus_ret_t nbus_channel_send(NbusChannel *self, nbus_endpoint_t ep, void *buf, size_t len) {
	if (len > NBUS_CHANNEL_MTU) {
		return NBUS_RET_FAILED;
	}

	if (self->channel_assigned == false) {
		return NBUS_RET_VOID;
	}

	blake2s_state b2;
	blake2s_init_key(&b2, NBUS_SIV_LEN, self->km, B2S_KM_LEN);

	/* Send a leading frame first. */
	struct nbus_id lf = {
		.channel = self->channel,
		.multicast = self->multicast,
		.response = self->peripheral,
		.opcode = NBUS_OP_LEADING_MIN + ep
	};
	if (self->peripheral == false) {
		self->packet_counter++;
	}
	struct nbus_leading_frame_msg lfbuf = {
		/* For a peripheral mode channel, a packet is first received from the initiator saving the
		 * request packet counter. The response simply copies the packet counter value.
		 * For a initiator mode, the packet_counter variable contains the next valid value to be sent
		 * (it is incremented beforehand). When waiting for a response, we are expecting the same
		 * value as we sent before. */
		.counter = self->packet_counter,
		.len = len,
		.flags = 0
	};
	nbus_channel_send_frame(self, &lf, &lfbuf, sizeof(lfbuf));
	blake2s_update(&b2, &lfbuf, sizeof(lfbuf));

	/* Send the intermediate packet data. */
	size_t rem = len;
	uint8_t *bbuf = buf;
	uint8_t f_id = 0;
	while (rem) {
		/** @todo get the maximum size of the chunk somewhere */
		size_t chunk = 8;
		if (rem < chunk) {
			chunk = rem;
		}

		/* Construct and send the frame containing the chunk data. */
		struct nbus_id fragment = {
			.channel = self->channel,
			.multicast = self->multicast,
			.response = self->peripheral,
			.opcode = NBUS_OP_DATA_MIN + f_id
		};
		nbus_channel_send_frame(self, &fragment, bbuf, chunk);
		blake2s_update(&b2, bbuf, chunk);

		/* Advance to the next chunk */
		f_id ++;
		rem -= chunk;
		bbuf += chunk;
	}

	/* Send a trailing frame. */
	struct nbus_id tf = {
		.channel = self->channel,
		.multicast = self->multicast,
		.response = self->peripheral,
		.opcode = NBUS_OP_TRAILING
	};
	uint8_t siv[NBUS_SIV_LEN] = {0};
	blake2s_final(&b2, siv, NBUS_SIV_LEN);
	nbus_channel_send_frame(self, &tf, siv, 8);

	return NBUS_RET_OK;
}


nbus_ret_t nbus_channel_set_rx_handler(NbusChannel *self, nbus_ret_t (*rx_handler)(void *context, nbus_endpoint_t ep, void *buf, size_t len), void *context) {
	self->rx_handler = rx_handler;
	self->rx_handler_context = context;
	return NBUS_RET_OK;
}


nbus_ret_t nbus_channel_set_descriptor(NbusChannel *self, nbus_ret_t (*descriptor)(void *context, uint8_t id, void *buf, size_t *len), void *context) {
	self->descriptor = descriptor;
	self->descriptor_context = context;
	return NBUS_RET_OK;
}


static nbus_ret_t nbus_string_descriptor(void *context, uint8_t id, void *buf, size_t *len) {
	NbusChannel *self = (NbusChannel *)context;
	if (id < self->descriptor_strings_count) {
		*len = strlcpy((char *)buf, *(self->descriptor_strings + id), NBUS_DESCRIPTOR_LEN);
		return NBUS_RET_OK;
	}
	return NBUS_RET_FAILED;
}


nbus_ret_t nbus_channel_set_descriptor_strings(NbusChannel *self, const char * const *strings, size_t count) {
	self->descriptor_strings = strings;
	self->descriptor_strings_count = count;

	nbus_channel_set_descriptor(self, nbus_string_descriptor, self);
	return NBUS_RET_OK;
}


nbus_ret_t nbus_channel_set_short_id(NbusChannel *self, short_id_t short_id) {
	self->short_id = short_id;
	return NBUS_RET_OK;
}


nbus_ret_t nbus_channel_set_long_id(NbusChannel *self, const void *long_id, size_t len) {
	self->long_id = long_id;
	self->long_id_len = len;
	halfsiphash(long_id, len, (void *)siphash_key, (uint8_t *)&self->short_id, 4);

	/* XOR with the channel name */
	short_id_t name_id = 0;
	halfsiphash(self->name, strlen(self->name), (void *)siphash_key, (uint8_t *)&name_id, sizeof(name_id));
	self->short_id ^= name_id;

	return NBUS_RET_OK;
}


nbus_ret_t nbus_init(Nbus *self, Can *can) {
	memset(self, 0, sizeof(Nbus));
	self->can = can;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialising NBUS protocol driver"));

	/* Create channel for the discovery protocol. This channel is statically assigned. */
	nbus_channel_init(&self->discovery_channel, "discovery");
	nbus_add_channel(self, &self->discovery_channel);
	nbus_channel_set_rx_handler(&self->discovery_channel, &nbus_discovery_rx_handler, (void *)self);
	self->discovery_channel.peripheral = true;
	self->discovery_channel.multicast = true;
	self->discovery_channel.channel_assigned = true;
	self->discovery_channel.inhibit_discovery = true;

	/* We need to respond fast to NBUS requests over CAN-FD. Set to a higher priority. */
	xTaskCreate(nbus_receive_task, "nbus-rx", configMINIMAL_STACK_SIZE + 256, (void *)self, 2, &(self->receive_task));
	if (self->receive_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	return NBUS_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("init failed"));
	return NBUS_RET_FAILED;
}
