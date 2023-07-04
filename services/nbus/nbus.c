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
#include "cc-rpc.h"

#define MODULE_NAME "nbus"

/**
 * @brief NBUS tl;dr
 *
 * NBUS uses a CAN2.0b or CAN-FD frames with 29 bit identifiers arranged as:
 * rrrr cccc cccc cccc cccc DDrr oooo oooo
 *
 * - r - reserved for future use
 * - c - 16 bit channel identifier
 * - DD - request = 01, response = 01, publish = 10, subscribe = 11
 * - o - opcode, these bits are the last, all opcodes within a channel are equal priority
 *
 * Opcodes:
 * 0x00-0x3f - leading fragment, endpoints 0-63, 0 is the descriptor endpoint
 *             counter (4 bytes)
 *             frame length (2 bytes)
 *             flags (2 bytes)
 * 0x40-0xbf - data fragment
 * 0xc0      - blake2s-SIV trailing fragment
 *             MAC (8, 16 or 32 bytes allowed)
 * 0xc1      - short-ID advertisement
 *
 * @todo
 *
 * - change the way descriptors are read. Transform EP0 to a C&C EP with functions
 *   to set or get various parameters, single or multiple. Use CBOR, ideally.
 * - introduce a TFTP protocol equivalent
 * - introduce a bootloader configuration protocol
 */



/**********************************************************************************************************************
 * nbus layer 2 frame implementation
 **********************************************************************************************************************/

void nbus_parse_id(uint32_t id, struct nbus_id *sid) {
	sid->channel = (id >> 12) & 0xffff;
	sid->opcode = id & 0xff;
	sid->direction = (id >> 10) & 0x3;
}


uint32_t nbus_build_id(struct nbus_id *id) {
	uint32_t r = 0U;
	r += id->channel << 12;
	r += id->direction << 10;
	r += id->opcode;
	return r;
}


/**********************************************************************************************************************
 * nbus layer 3 packet implementation (receive and transmit)
 **********************************************************************************************************************/

/**
 * @brief Initialise a transmit packet instance
 *
 * Beware the data buffer is not yet assigned nor allocated. Use @p nbus_txpacket_buf_ref or @p nbus_txpacket_buf_copy
 * to set the packet data.
 */
static nbus_ret_t nbus_txpacket_init(NbusTxPacket *self, nbus_channel_id_t channel_id, nbus_endpoint_t ep, uint32_t packet_counter) {
	memset(self, 0, sizeof(NbusTxPacket));

	self->channel_id = channel_id;
	self->ep = ep;
	self->packet_counter = packet_counter;
	self->state = NBUS_TXP_LEADING;
	return NBUS_RET_OK;
}


static nbus_ret_t nbus_txpacket_buf_ref(NbusTxPacket *self, void *buf, size_t len) {
	self->buf = buf;
	self->len = len;
	return NBUS_RET_OK;
}


static nbus_ret_t nbus_txpacket_buf_copy(NbusTxPacket *self, void *buf, size_t len) {
	(void)self;
	(void)buf;
	(void)len;
	/** todo malloc() and copy */
	return NBUS_RET_FAILED;
}

static nbus_ret_t nbus_txpacket_get_fragment(NbusTxPacket *self, struct nbus_id *id, void *data, size_t *len) {
	switch (self->state) {
		case NBUS_TXP_LEADING: {
			id->channel = self->channel_id;
			id->direction = NBUS_DIR_RESPONSE;
			id->opcode = NBUS_OP_LEADING_MIN + self->ep;

			struct nbus_leading_frame_msg lfbuf = {
				/* For a peripheral mode channel, a packet is first received from the initiator saving the
				 * request packet counter. The response simply copies the packet counter value.
				 * For a initiator mode, the packet_counter variable contains the next valid value to be sent
				 * (it is incremented beforehand). When waiting for a response, we are expecting the same
				 * value as we sent before. */
				.counter = self->packet_counter,
				.len = self->len,
				.flags = 0
			};
			memcpy(data, &lfbuf, sizeof(lfbuf));
			*len = sizeof(lfbuf);

			self->packet_sent = 0;
			self->next_frag = 0;
			self->state = NBUS_TXP_DATA;
			break;
		}
		case NBUS_TXP_DATA: {
			/* Construct and send the frame containing the chunk data. */
			id->channel = self->channel_id;
			id->direction = NBUS_DIR_RESPONSE;
			id->opcode = NBUS_OP_DATA_MIN + self->next_frag;

			size_t frag_len = self->len - self->packet_sent;
			/** @todo limit properly to CAN MTU */
			if (frag_len > 8) {
				frag_len = 8;
			}
			memcpy(data, (uint8_t *)self->buf + self->packet_sent, frag_len);
			*len = frag_len;

			self->packet_sent += frag_len;
			self->next_frag++;
			if (self->packet_sent == self->len) {
				self->state = NBUS_TXP_TRAILING;
			}
			break;
		}
		case NBUS_TXP_TRAILING: {
			id->channel = self->channel_id;
			id->direction = NBUS_DIR_RESPONSE;
			id->opcode = NBUS_OP_TRAILING;

			uint8_t siv[NBUS_SIV_LEN] = {0};
			memcpy(data, siv, sizeof(siv));
			*len = sizeof(siv);

			self->state = NBUS_TXP_DONE;
			break;
		}
		case NBUS_TXP_DONE:
			return NBUS_RET_VOID;

		case NBUS_TXP_EMPTY:
		default:
			return NBUS_RET_FAILED;
	}

	return NBUS_RET_OK;
}


static nbus_ret_t nbus_rxpacket_init(NbusRxPacket *self, size_t max_size) {
	memset(self, 0, sizeof(NbusRxPacket));

	self->buf = malloc(max_size);
	if (self->buf == NULL) {
		return NBUS_RET_FAILED;
	}
	self->buf_size = max_size;

	self->done = xSemaphoreCreateBinary();
	if (self->done == NULL) {
		return NBUS_RET_FAILED;
	}

	self->state = NBUS_RXP_READY;
	return NBUS_RET_OK;
}


static nbus_ret_t nbus_rxpacket_free(NbusRxPacket *self) {
	free(self->buf);
	self->buf = NULL;
	self->buf_size = 0;
	vSemaphoreDelete(self->done);
	return NBUS_RET_OK;
}


static nbus_ret_t nbus_rxpacket_reset(NbusRxPacket *self) {
	self->state = NBUS_RXP_READY;
	return NBUS_RET_OK;
}


static nbus_ret_t nbus_rxpacket_wait(NbusRxPacket *self, uint32_t timeout_ms) {
	if (self->state == NBUS_RXP_DONE) {
		return NBUS_RET_OK;
	}
	if (xSemaphoreTake(self->done, timeout_ms) == pdTRUE) {
		return NBUS_RET_OK;
	}

	return NBUS_RET_VOID;
}


static nbus_ret_t nbus_rxpacket_append(NbusRxPacket *self, struct nbus_id *id, void *buf, size_t len) {
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("rxpacket state %d, channel %u, op %u"), self->state, id->channel, id->opcode);

	if (self->state == NBUS_RXP_EMPTY) {
		return NBUS_RET_FAILED;
	}

	switch (id->opcode) {
		case NBUS_OP_LEADING_MIN ... NBUS_OP_LEADING_MAX: {
			/* Leading frame is always dominant wrt. packet processing. Whatever the actual state is,
			 * a leading frame always causes its reset. Consider generating a warning if a different
			 * endpoint packet is currently being assembled as this means the endpoint data is
			 * interleaved (forbidden condition). */
			nbus_endpoint_t ep = id->opcode - NBUS_OP_LEADING_MIN;
			struct nbus_leading_frame_msg *lf = buf;

			/* The previous packet was correctly finished and set to the DONE state but nobody
			 * bothered to get its data and reset the FSM back. */
			if (self->state == NBUS_RXP_DONE) {
				u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("nobody receiving"));
				self->state = NBUS_RXP_READY;
			}

			if (self->state != NBUS_RXP_READY) {
				/* Nope. Go back to ready, restart packet processing. */
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("packet reassembly restarted state=%d"), self->state);
				self->state = NBUS_RXP_READY;
			}

			/* Do not accept packets longer than the allowed MTU. Do not even bother receiving
			 * the rest of the packet (do not init). */
			if (lf->len > self->buf_size) {
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("packet len > MTU (%u)"), lf->len);
				return NBUS_RET_FAILED;
			}

			/* Loading frame accepted now. */
			self->packet_size = 0;
			self->expected_packet_size = lf->len;
			self->frame_expected = 0;
			self->ep = ep;

			self->state = NBUS_RXP_DATA;

			// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("new packet channel %u, ep %u, size %u"), self->channel_id, self->ep, self->packet_size);
			break;
		}

		case NBUS_OP_DATA_MIN ... NBUS_OP_DATA_MAX: {
			if (self->state != NBUS_RXP_DATA) {
				return NBUS_RET_FAILED;
			}
			uint32_t frame = id->opcode - NBUS_OP_DATA_MIN;
			if (self->frame_expected != frame) {
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("enexpected frame %u"), frame);
				return NBUS_RET_FAILED;
			}
			/* Do not receive more data than is the maximum packet size (checked during the leading
			 * frame processing). */
			if ((self->packet_size + len) > self->expected_packet_size) {
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("packet becoming too big %lu > %lu"), self->packet_size + len, self->expected_packet_size);
				return NBUS_RET_FAILED;
			}

			memcpy(self->buf + self->packet_size, buf, len);
			self->packet_size += len;
			self->frame_expected++;

			if (self->packet_size == self->expected_packet_size) {
				self->state = NBUS_RXP_TRAILING;
			}
			break;
		}

		case NBUS_OP_TRAILING: {
			if (self->state != NBUS_RXP_TRAILING) {
				/* Wrong state, log it. Do not process the data and invalidate FSM. */
				// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("invalid trailing frame opcode, packet incomplete"));
				self->state = NBUS_RXP_INVALID;
				return NBUS_RET_FAILED;
			}
			/* Do not check packet size. It is correct if the state is ok. */

			/** @todo check the MAC here */

			self->state = NBUS_RXP_DONE;
			xSemaphoreGive(self->done);
			break;
		}

		case NBUS_OP_ADVERTISEMENT:
			/* An advertisement opcode has been received for the same channel-id. This basically means
			 * there is a channel-id conflict on the bus. Retreat by invalidating the current channel-id. */
			return NBUS_RET_INVALID;

		default:
			return NBUS_RET_FAILED;
	}
	return NBUS_RET_OK;
}


/**********************************************************************************************************************
 * nbus channel implementation
 **********************************************************************************************************************/

nbus_ret_t nbus_add_channel(Nbus *self, NbusChannel *channel) {
	if (channel == NULL) {
		return NBUS_RET_BAD_PARAM;
	}
	channel->next = self->first;
	channel->nbus = self;
	self->first = channel;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("add channel sid=0x%08x"), channel->short_id);

	return NBUS_RET_OK;
}


nbus_ret_t nbus_channel_process_cc(NbusChannel *self, void *buf, size_t len) {
	if (cbor_rpc_parse(&self->ccrpc, buf, len) != CBOR_RPC_RET_OK ||
	    cbor_rpc_run(&self->ccrpc) ||
	    cbor_rpc_get_response(&self->ccrpc, &buf, &len)) {
		return NBUS_RET_FAILED;
	}
	nbus_channel_send(self, 0, buf, len);

	return NBUS_RET_OK;
}


nbus_ret_t nbus_channel_receive(NbusChannel *self, nbus_endpoint_t *ep, void *buf, size_t buf_size, size_t *len, uint32_t timeout_ms) {
	nbus_ret_t ret = nbus_rxpacket_wait(&self->rxpacket, timeout_ms);
	if (ret != NBUS_RET_OK) {
		return ret;
	}

	/** @todo steal this thread for EP 0 processing. Check if ep == 0 and call c&c process. Return VOID. */
	if (self->rxpacket.ep == 0) {
		nbus_channel_process_cc(self, self->rxpacket.buf, self->rxpacket.packet_size);
		nbus_rxpacket_reset(&self->rxpacket);
		return NBUS_RET_VOID;
	}

	if (self->rxpacket.packet_size > buf_size) {
		nbus_rxpacket_reset(&self->rxpacket);
		return NBUS_RET_BIG;
	}
	memcpy(buf, self->rxpacket.buf, self->rxpacket.packet_size);
	*len = self->rxpacket.packet_size;
	*ep = self->rxpacket.ep;

	nbus_rxpacket_reset(&self->rxpacket);
	return NBUS_RET_OK;
}



static NbusChannel *nbus_channel_find_by_id(Nbus *self, nbus_channel_id_t id) {
	NbusChannel *ch = self->first;
	NbusChannel *found = NULL;
	while (ch) {
		if (ch->channel_id_valid && ch->channel_id == id) {
			found = ch;
			break;
		}
		ch = ch->next;
	}
	return found;
}


static void nbus_receive_task(void *p) {
	Nbus *self = (Nbus *)p;
	struct can_message msg = {0};
	struct nbus_id id;

	while (true) {
		/** @todo adjust the timeout */
		if (self->can->vmt->receive(self->can, &msg, 10000) != CAN_RET_OK) {
			continue;
		}
		if (msg.extid == false) {
			continue;
		}
		nbus_parse_id(msg.id, &id);

		/* Try to find the corresponding channel. Do nothing if not found. */
		NbusChannel *channel = nbus_channel_find_by_id(self, id.channel);
		if (channel == NULL) {
			continue;
		}

		/* Push the frame into the RX packet FSM. Check the result. */
		nbus_ret_t ret = nbus_rxpacket_append(&channel->rxpacket, &id, msg.buf, msg.len);
		if (ret == NBUS_RET_OK) {
			if (channel->rxpacket.state == NBUS_RXP_DATA) {
				/* Reception started, block any transmit attempt. */
				/** @todo lock here */
			} else if (channel->rxpacket.state == NBUS_RXP_DONE) {
				/* The packet is complete, unlock the receiving tread if required. */
				/** @todo unlock rx */
			}
		} else if (ret == NBUS_RET_INVALID) {
			channel->channel_id_valid = false;
		}
	}
	vTaskDelete(NULL);
}


static cbor_rpc_ret_t nbus_channel_cc_read_name(CborRpc *self, char *buf, size_t buf_size) {
	NbusChannel *channel = self->parent;
	strncpy(buf, channel->name, buf_size);
	return CBOR_RPC_RET_OK;
}


static cbor_rpc_ret_t nbus_channel_cc_read_parent(CborRpc *self, char *buf, size_t buf_size) {
	NbusChannel *channel = self->parent;
	if (channel->parent != NULL) {
		snprintf(buf, buf_size, "%08lx", channel->parent->short_id);
	} else {
		strcpy(buf, "");
	}
	return CBOR_RPC_RET_OK;
}


nbus_ret_t nbus_channel_init(NbusChannel *self, const char *name) {
	memset(self, 0, sizeof(NbusChannel));
	self->name = name;
	cbor_rpc_init(&self->ccrpc);
	self->ccrpc.parent = self;

	self->name_handler.name = "name";
	self->name_handler.htype = CBOR_RPC_HTYPE_STRING;
	self->name_handler.v_string.read = &nbus_channel_cc_read_name;
	cbor_rpc_add_handler(&self->ccrpc, &self->name_handler);

	self->parent_handler.name = "parent";
	self->parent_handler.htype = CBOR_RPC_HTYPE_STRING;
	self->parent_handler.v_string.read = &nbus_channel_cc_read_parent;
	cbor_rpc_add_handler(&self->ccrpc, &self->parent_handler);

	/* RX packet instance is created only once. */
	nbus_rxpacket_init(&self->rxpacket, NBUS_CHANNEL_MTU);

	return NBUS_RET_OK;
}


static nbus_ret_t nbus_channel_send_frame(NbusChannel *self, struct nbus_id *id, void *buf, size_t len) {
	Nbus *nbus = self->nbus;

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
		return NBUS_RET_BAD_PARAM;
	}
	if (self->channel_id_valid == false) {
		return NBUS_RET_VOID;
	}

	/** @todo acquire channel lock */
	nbus_txpacket_init(&self->txpacket, self->channel_id, ep, self->packet_counter);
	nbus_txpacket_buf_ref(&self->txpacket, buf, len);

	struct nbus_id sid = {0};
	uint8_t framebuf[8];
	size_t framelen = 0;
	while (nbus_txpacket_get_fragment(&self->txpacket, &sid, framebuf, &framelen) == NBUS_RET_OK) {
		nbus_channel_send_frame(self, &sid, framebuf, framelen);
	}
	/** @todo release channel lock */

	return NBUS_RET_OK;
}


nbus_ret_t nbus_channel_set_explicit_short_id(NbusChannel *self, nbus_short_id_t short_id) {
	self->short_id = short_id;
	return NBUS_RET_OK;
}


nbus_ret_t nbus_channel_set_parent(NbusChannel *self, NbusChannel *parent) {
	/* Create the current channel short-id by hashing parent's short-id with
	 * the current channel name. */

	blake2s_state b2;
	blake2s_init(&b2, sizeof(nbus_short_id_t));
	blake2s_update(&b2, &parent->short_id, sizeof(parent->short_id));
	blake2s_update(&b2, self->name, strlen(self->name));
	blake2s_final(&b2, &self->short_id, sizeof(self->short_id));

	self->parent = parent;
	return NBUS_RET_OK;
}


static void advertise_channel_id(NbusChannel *self) {
	struct nbus_id cha = {
		.channel = self->channel_id,
		.direction = NBUS_DIR_PUBLISH,
		.opcode = NBUS_OP_ADVERTISEMENT,
	};
	// u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("advertising channel ID %d for 0x%08x"), self->channel_id, self->short_id);
	nbus_channel_send_frame(self, &cha, &self->short_id, sizeof(self->short_id));
}


static void nbus_send_adv(Nbus *self) {
	NbusChannel *ch = self->first;
	while (ch) {
		if (ch->channel_id_valid) {
			ch->adv_time++;
			if (ch->adv_time >= NBUS_ADV_TIME) {
				ch->adv_time = 0;
				advertise_channel_id(ch);
			}
		}
		ch = ch->next;
	}
}


static void nbus_generate_short_ids(Nbus *self) {
	blake2s_state b2;

	NbusChannel *ch = self->first;
	while (ch) {
		if (ch->channel_id_valid == false) {
			/* If the channel-id is not yet valid (the channel is new), channel_id member contains
			 * zero as the spec mandates. Channel-id for a new channel is created from the current
			 * channel-id with the same algo using the zero value. */
			blake2s_init(&b2, sizeof(nbus_channel_id_t));
			blake2s_update(&b2, &ch->short_id, sizeof(ch->short_id));
			blake2s_update(&b2, &ch->channel_id, sizeof(ch->channel_id));
			blake2s_final(&b2, &ch->channel_id, sizeof(ch->channel_id));
			ch->channel_id_valid = true;
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("assign new channel ID %d for 0x%08x"), ch->channel_id, ch->short_id);
			advertise_channel_id(ch);
		}
		ch = ch->next;
	}
}


static void nbus_housekeeping_task(void *p) {
	Nbus *self = (Nbus *)p;

	while (true) {
		nbus_send_adv(self);

		nbus_generate_short_ids(self);
		vTaskDelay(1000);
	}
	vTaskDelete(NULL);
}


nbus_ret_t nbus_init(Nbus *self, Can *can) {
	memset(self, 0, sizeof(Nbus));
	self->can = can;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialising NBUS protocol driver"));

	/* We need to respond fast to NBUS requests over CAN-FD. Set to a higher priority. */
	xTaskCreate(nbus_receive_task, "nbus-rx", configMINIMAL_STACK_SIZE + 256, (void *)self, 2, &(self->receive_task));
	if (self->receive_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create receiving task"));
		goto err;
	}

	xTaskCreate(nbus_housekeeping_task, "nbus-hk", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->housekeeping_task));
	if (self->housekeeping_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create housekeeping task"));
		goto err;
	}

	return NBUS_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("init failed"));
	return NBUS_RET_FAILED;
}
