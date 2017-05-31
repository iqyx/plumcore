/**
 * uMeshFw uMesh receive task
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "u_assert.h"
#include "u_log.h"
#include "hal_module.h"
#include "module_umesh.h"
#include "interface_mac.h"
#include "umesh_l2_receive.h"
#include "umesh_l2_pbuf.h"
#include "crc.h"
#include "umesh_l2_crypto.h"


static const uint8_t test_key[] = {
	0xb0, 0x03, 0x44, 0xb5, 0xbe, 0x2c, 0x1e, 0x3c, 0x91, 0x29, 0xfe, 0x27, 0x2d, 0x99, 0x10, 0x90,
	0x3f, 0xce, 0x32, 0xc5, 0x68, 0x16, 0x56, 0x37, 0xfe, 0xda, 0xa8, 0x53, 0x70, 0xae, 0xd1, 0x10
};

/**
 * @section node-identification Node identification
 *
 * Communication on the 2nd layer can occur between adjacent nodes only. Many
 * legacy protocols use Unique Identifiers such as EUI-48 (48-bit Extended
 * Unique Identifier) to identify layer 2 network participants. As a result,
 * 12 additional bytes are transferred in each frame which introduces
 * significant overhead in low data rate networks. Such layer 2 identifiers are
 * only required to uniquely distinguish network nodes in the same broadcast
 * domain and can be considerably shortened for this purpose. However with the
 * lack of central authority managing these identifiers, identifier collision
 * problem have to be addressed.
 *
 * Every node uses TID (temporary identifier) for its identification. TID is any
 * randomly choosen unsigned number from 1 to 2^32 - 1. Every node MUST generate
 * its TID from random source at least on power-up. It SHOULD generate new TID
 * after configurable time has elapsed to maintain some minimal degree of node
 * anonymity. A node is not required to generate its TID from the whole 32bit
 * space, eg. new identifiers could be generated from 0-255 range (risking
 * higher probability of collisions).
 *
 * A node accepts received packet only if one of the following criteria is met:
 *
 *   - received packet has broadcast flag set
 *   - received packet "dtid" value is same as current receiving node TID
 *   - received packet "dtid" value is same asi previous value of TID,
 *     but only for a configurable amount of time after new TID becomes
 *     efficient
 *
 * It means that if identifier collision occurs, the same data is received by
 * multiple nodes. However, this situation is prevented in practice:
 *
 *   - if the two nodes have already finished layer 2 key exchange, colliding
 *     node cannot receive data because of key mismatch
 *   - a node cannot initiate key exchange with another node with the same TID,
 *     hence no communication will be done between colliding nodes
 *   - if a node want to initiate key exchange with another node which has TID
 *     colliding with a third node, key exchange will fail in first
 *     (Diffie-Hellman) phase as a result of multiple incompatible received
 *     answers
 *
 */

/**
 * Collecting packets from multiple MACs is done in this task. The execution is
 * halted until a semaphore is signalled which means that at least one MAC has
 * some data available. All MACs are then checked for data and the data is then
 * serially pushed as an argument to the umesh_l2_receive function for further
 * processing.
 * The semaphore is signalled using callback functions which are registered
 * during the initialization and MAC addition (module_umesh).
 *
 * The main reason for processing data serially from all MACs in this one task
 * is to save some stack space.
 */
void umesh_l2_receive_task(void *p) {

	struct module_umesh *umesh = (struct module_umesh *)p;

	while (1) {
		struct interface_mac_packet_info info;
		uint8_t buf[UMESH_L2_SANDBOX_SIZE];
		size_t len;

		/* TODO: wait here. */
		if (umesh->macs[0] != NULL) {
			if (interface_mac_receive(umesh->macs[0], buf, sizeof(buf), &len, &info) == INTERFACE_MAC_RECEIVE_OK) {
				/* Process the received packet. */
				umesh_l2_receive(umesh, buf, len, &info, 0);
			}
		} else {
			vTaskDelay(100);
		}
	}

	vTaskDelete(NULL);
}


/**
 * Layer2 receive check is called before any layer 2 processing is done. It
 * checks the packet buffer from the lower layer (@p data, @p len, @p info). Any
 * processing is then done only on the raw data part of the packet buffer
 * (@p data and @p len). The return value indicates whether the packet should
 * be processes or if it should be silently discarded (counters are updated in
 * both cases).
 */
int32_t umesh_l2_receive_check(struct module_umesh *umesh, const uint8_t *data, size_t len, struct interface_mac_packet_info *info) {
	if (u_assert(umesh != NULL && data != NULL && info != NULL)) {
		return UMESH_L2_RECEIVE_CHECK_FAILED;
	}

	/* The packet is too big and there is a possibility it won't fit into
	 * at least one of the used buffers. */
	if (len > UMESH_L2_PACKET_SIZE) {
		return UMESH_L2_RECEIVE_CHECK_FAILED;
	}

	/* The packet is that small that even the header cannot be contained
	 * withing the available data. It is discarded before processing. */
	if (len < 2) {
		return UMESH_L2_RECEIVE_CHECK_FAILED;
	}

	return UMESH_L2_RECEIVE_CHECK_OK;
}



/**
 * @section l2-packet-structure L2 packet structure
 *
 * Each layer 2 packet is made of fields. Some fields are optional and their
 * inclusion in the packet depends on flags in the control field.
 *
 * Field		Size		Description
 * ---------------------------------------------------------------
 * Control              1-n bytes       Packet control field
 * Dst TID (dtid)       1-5 bytes       Destination TID
 * Src TID (stid)       1-5 bytes       TID of the source
 * Data                 n bytes         Plaintext or encrypted data payload
 * MAC                  n bytes         Message authentication code (MAC) or CRC
 *
 * The minimum valid packet length is 2 bytes - one control byte with a
 * broadcast flag set and one source TID byte. The packet can contain no data.
 *
 * Destination TID ("dtid") of the packet is contained only if the "bcast" bit
 * in the control field is set to "0". In this case "dtid" field contain the TID
 * of the receiving node.
 *
 * Source TID MUST be contained in the packet.
 *
 */

/**
 * Called from the main reception task. The whole L2 parsing process is done in
 * this single function. The output is umesh_l2_pbuf structure.
 */
int32_t umesh_l2_receive(struct module_umesh *umesh, const uint8_t *data, size_t len, struct interface_mac_packet_info *info, uint32_t iface) {
	if (u_assert(umesh != NULL && data != NULL && info != NULL)) {
		return UMESH_L2_RECEIVE_FAILED;
	}

	/* If this pointer is not-null, the neighbor which sent this particular
	 * packet is known. */
	struct umesh_l2_nbtable_item *nbtable_item = NULL;

	/* Prepare the shared packet buffer first. */
	umesh_l2_pbuf_clear(&(umesh->l2_pbuf));
	umesh->l2_pbuf.flags = UMESH_L2_PBUF_FLAG_USED;
	umesh->l2_pbuf.iface = iface;
	umesh->l2_pbuf.rssi = info->rssi;
	umesh->l2_pbuf.fei = info->fei;
	umesh->l2_pbuf.bit_errors = info->bit_errors;

	/** @todo the last byte is always read */
	len--;

	if (umesh_l2_receive_check(umesh, data, len, info) != UMESH_L2_RECEIVE_CHECK_OK) {
		goto err;
	}

	/* Temporary pointer holding the actual position of the data. */
	const uint8_t *pos = data;

	/* Parse the packet control header. */
	if (umesh_l2_parse_control(&pos, len - (pos - data), &(umesh->l2_pbuf)) != UMESH_L2_PARSE_CONTROL_OK) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_receive: malformed control", umesh->module.name);
		}
		goto err;
	}

	/* Parse the DTID if required (if the packet is not broadcast). */
	if (!(umesh->l2_pbuf.flags & UMESH_L2_PBUF_FLAG_BROADCAST)) {
		if (umesh_l2_parse_tid(&pos, len - (pos - data), &(umesh->l2_pbuf.dtid)) != UMESH_L2_PARSE_TID_OK) {
			if (umesh->l2_debug_packet_rx_error) {
				u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_receive: malformed DTID", umesh->module.name);
			}
			goto err;
		}
	}

	/* Parse the STID. It is always present. */
	if (umesh_l2_parse_tid(&pos, len - (pos - data), &(umesh->l2_pbuf.stid)) != UMESH_L2_PARSE_TID_OK) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_receive: malformed STID", umesh->module.name);
		}
		goto err;
	}

	/* If the STID was parsed successfully, we can assign the packet to an
	 * existing neighbor (if it is added in the neighbor table). We will
	 * use this information to update per-neighbor statistics. */
	uint8_t *peer_key = NULL;
	uint32_t peer_key_len = 0;
	if (umesh_l2_nbtable_find(&(umesh->nbtable), umesh->l2_pbuf.stid, &nbtable_item) == UMESH_L2_NBTABLE_FIND_OK) {
		umesh->l2_pbuf.known_neighbor = true;

		/** @todo */
		//~ peer_key = nbtable_item->rxkey;
		//~ peer_key_len = UMESH_L2_NBTABLE_RXKEY_SIZE;
		peer_key = test_key;
		peer_key_len = 32;
	}

	/* Parse and optionally decrypt/authenticate the packet data. Start of
	 * the header data is passed as the first argument to allow the header
	 * to be authenticated together with the packet data. */
	if (umesh_l2_parse_data(data, &pos, len - (pos - data), &(umesh->l2_pbuf), peer_key, peer_key_len) != UMESH_L2_PARSE_DATA_OK) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_receive: data failed", umesh->module.name);
		}
		goto err;
	}

	/* Empty packets are valid by definition, but we do not forward them
	 * to higher layers. */
	if (umesh->l2_pbuf.len == 0) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_receive: empty packet dropped", umesh->module.name);
		}
		goto err;
	}

	if (umesh_l2_receive_route_pbuf(umesh, &(umesh->l2_pbuf)) != UMESH_L2_RECEIVE_ROUTE_PBUF_OK) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_receive: unroutable packet dropped", umesh->module.name);
		}
		goto err;
	}

	if (umesh->l2_debug_packet_rx) {
		u_log(system_log, LOG_TYPE_DEBUG,
			"%s: umesh_l2_receive: packet received, stid=%u dtid=%u rssi=%d",
			umesh->module.name,
			umesh->l2_pbuf.stid,
			umesh->l2_pbuf.dtid,
			umesh->l2_pbuf.rssi
		);
	}

	if (umesh->l2_pbuf.proto > UMESH_L3_PROTOCOL_COUNT) {
		goto err;
	}

	if (umesh->l3_protocols[umesh->l2_pbuf.proto] != NULL && umesh->l3_protocols[umesh->l2_pbuf.proto]->receive_handler != NULL) {
		umesh->l3_protocols[umesh->l2_pbuf.proto]->receive_handler(
			&(umesh->l2_pbuf),
			umesh->l3_protocols[umesh->l2_pbuf.proto]->context
		);
	} else {
		goto err;
	}

	/* Update per-neighbor statistics if the neighbor is known. */
	if (nbtable_item != NULL) {
		nbtable_item->rx_bytes += len;
		nbtable_item->rx_packets++;
	}
	return UMESH_L2_RECEIVE_OK;

err:
	/* Update per-neighbor statistics if the neighbor is known. */
	if (nbtable_item != NULL) {
		nbtable_item->rx_bytes += len;
		nbtable_item->rx_packets++;
		nbtable_item->rx_bytes_dropped += len;
		nbtable_item->rx_packets_dropped++;
	}
	return UMESH_L2_RECEIVE_FAILED;
}


/**
 * @section l2-packet-control-field L2 Packet control field
 *
 * Packet control field is encoded in a variable length manner to reduce the
 * overhead of small packets with common payload and parameters. In every byte
 * of the packet control field, only 7 least significant bits (LSb's) are used
 * to carry an information, MSb is used to indicate if more bytes follow. This
 * MSb is called an "ext" bit (extension) and is set to "1" if another byte
 * follows and to "0" if it is the last one.
 *
 *
 * Byte 1 of the packet control field
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | 7      | 6      | 5      | 4      | 3      | 2      | 1      | 0      |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | Ext    | bcast  | Enc1   | Enc0   | L3p1   | L3p0   | Pad    |Res     |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 *
 * Ext -            set to "1" if another byte follows
 * bcast -          if set to "1", packet is broadcast to all nodes in the
 *                  range. In this case, no DTID field is present in the packet
 * Enc1, Enc0 -     2 bits of packet encryption and authentication mode
 * L3p1, L3p0 -     2 bits of L3 protocol. L3p0 is the least significant bit.
 * Res -            not used, SHOULD be set to 0
 * Pad -            1 byte padding is added to the packet data if set
 *
 * @todo The Pad bit was added non-conceptually to allow a lower layer (MAC
 *       layer) correctly determine the exact length of a data it contains.
 *       This bit is tightly coupled to the algorithm used for error correction
 *       (24, 12 Golay coding) and will be removed. It MUST NOT be set nor used
 *       by a layer 2 implementation.
 *
 *
 * Byte 2 of packet control field
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | 15     | 14     | 13     | 12     | 11     | 10     | 9      | 8      |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | Ext    | Enc2   | Reserved                          | L3p3   | L3p2   |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 *
 * Ext -            set to "1" if another byte follows. MUST be 0.
 * Enc2 -           1 bit of packet encryption and authentication mode
 * L3p3, L3p2 -     another 2 bits if L3 protocol
 * Reserved -       not used, SHOULD be set to 0
 *
 * The most common encryption and packet authentication methods are listed
 * in the first 4 lines with Enc2 bit set to "0". This allows the packet
 * to use a single byte control field to select the most common encryption and
 * authentication modes.
 *
 * The same apply to layer 3 protocols - the most commonly used protocols have
 * assigned identifiers from 0 to 3 to allow them to be specified with only
 * 1 byte control field.
 *
 * @note Enc2 bit is not always present in the control field. It is read as "0"
 *       in this case (eg. when control field is 1 byte only)
 *
 * @note Enc3 bit can be added in the future to extend the list of available
 *       ciphers.
 */
int32_t umesh_l2_parse_control(const uint8_t **data, uint32_t len, struct umesh_l2_pbuf *pbuf) {
	if (u_assert(data != NULL && *data != NULL && pbuf != NULL)) {
		return UMESH_L2_PARSE_CONTROL_FAILED;
	}

	/* There is not enough data to contain even single byte control field. */
	if (len < 1) {
		return UMESH_L2_PARSE_CONTROL_NO_DATA;
	}

	/* Get the first byte of the control field. Advance to the next byte
	 * and decrement the count of available bytes. */
	uint8_t byte1 = **data;
	(*data)++;
	len--;

	/* Check for the bcast (broadcast) flag. If set, save it in the pbuf. */
	/** @todo replace with a #define */
	if (byte1 & 0x40) {
		pbuf->flags |= UMESH_L2_PBUF_FLAG_BROADCAST;
	}

	/* Parse the Enc1 and Enc0 bits (encryption and authentication algo). */
	pbuf->sec_algo = ((byte1 >> 4) & 0x03);

	/* Parse the L3p1 and L3p0 bits (layer 3 protocol). */
	pbuf->proto = ((byte1 >> 2) & 0x03);

	/* If the extension flag of the first byte is set, try to get another
	 * byte. */
	/** @todo replace with a #define */
	if (byte1 & 0x80) {

		/* The Ext bit was set indicating that another control header
		 * byte should follow but no further data is available. Return -
		 * the control field is malformed. */
		if (len < 1) {
			return UMESH_L2_PARSE_CONTROL_NO_DATA;
		}

		/* If there is another byte available, read id. */
		uint8_t byte2 = **data;
		(*data)++;
		len--;

		/* Parse the Enc2 bit, extend the security algo with another bit
		 * (it allows the algorithms 4-7 to be specified. */
		/** @todo replace with a #define */
		if (byte2 & 0x40) {
			pbuf->sec_algo += 4;
		}

		/* Parse the L3p2 and L3p3 bits. */
		pbuf->proto = (pbuf->proto << 2) | (byte2 & 0x03);

		/* The second byte of the control field cannot have the Ext bit
		 * set. Return if set (packet malformed). */
		/** @todo replace with a #define */
		if (byte2 & 0x80) {
			return UMESH_L2_PARSE_CONTROL_UNFINISHED;
		}
	}

	return UMESH_L2_PARSE_CONTROL_OK;
}

/**
 * @section tid-format Destination and source TID format
 *
 * Destination and source TIDss are encoded in a manner similar to control
 * field encoding. Every byte can carry 7 bits of the TID number. The most
 * significant bit is set to "1" if another byte follows and to "0" otherwise
 * (it is the last byte of the TID).
 *
 * If the TID is lower than 128, a single byte can be used to encode it:
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | 7      | 6      | 5      | 4      | 3      | 2      | 1      | 0      |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | 0      | bit 6  | bit 5  | bit 4  | bit 3  | bit 2  | bit 1  | bit 0  |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 *
 * If it is lower than 16384, it can be encoded using 2 bytes:
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | 15     | 14     | 13     | 12     | 11     | 10     | 9      | 8      |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | 0      | bit 13 | bit 12 | bit 11 | bit 10 | bit 9  | bit 8  | bit 7  |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 *
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | 7      | 6      | 5      | 4      | 3      | 2      | 1      | 0      |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * | 1      | bit 6  | bit 5  | bit 4  | bit 3  | bit 2  | bit 1  | bit 0  |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 *
 * Up to 5 bytes can be used to encode whole 32bit TID.
 */
int32_t umesh_l2_parse_tid(const uint8_t **data, uint32_t len, uint32_t *tid) {
	/** @todo parameter check */

	*tid = 0;
	for (uint8_t i = 5; i >= 1; i--) {
		/* We are going to read the next TID byte, check if there is
		 * some data available. Return otherwise (malformed TID). */
		if (len < 1) {
			return UMESH_L2_PARSE_TID_NO_DATA;
		}

		/* Get the next byte and decrement the count of available
		 * bytes. */
		uint8_t byte = **data;
		(*data)++;
		len--;

		/* Shift the current value of the TID 7 bits left and append
		 * another 7 bits from the current byte. */
		(*tid) = ((*tid) << 7) | (byte & 0x7f);

		/* Finish the parsing if this was the last byte (ie. the most
		 * significat Ext bit is not set). */
		if ((byte & 0x80) == 0) {
			return UMESH_L2_PARSE_TID_OK;
		}
	}

	/* We shouldn't get here. If we do, it means that the Ext bit was set
	 * in all 5 bytes of the TID. Parsing was interrupted as this indicates
	 * a malformed packet (the TID was either unfinisned or too big). */
	return UMESH_L2_PARSE_TID_TOO_BIG;
}


int32_t umesh_l2_parse_counter(const uint8_t **data, uint32_t *len, uint16_t *counter) {
	if (*len < 2) {
		return UMESH_L2_PARSE_COUNTER_FAILED;
	}

	*counter = ((*data)[0] << 8) | (*data)[1];
	(*data) += 2;
	(*len) -= 2;

	return UMESH_L2_PARSE_COUNTER_OK;
}


/**
 * Parse the data part of the packet. The data may be in plaintext without any
 * checks or encryption, error checked, error corrected, encrypted or encrypted
 * and authenticated. Complete list of supported modes is in umesh_l2_receive,h
 */
int32_t umesh_l2_parse_data(const uint8_t *header, const uint8_t **data, uint32_t len, struct umesh_l2_pbuf *pbuf, uint8_t *key, uint32_t key_len) {
	if (u_assert(data != NULL && *data != NULL && pbuf != NULL && header != NULL && *data > header)) {
		return UMESH_L2_PARSE_DATA_FAILED;
	}

	/* Temporary variable holding the length of the authentication tag. */
	uint32_t tag_len = 0;

	/* Reset the security type of the packet. */
	pbuf->sec_type = UMESH_L2_PBUF_SECURITY_NONE;

	/* Header length is the difference between start of the data and the
	 * header pointer. It is used for CRC computation and packet
	 * authentication. */
	uint32_t header_len = (uint8_t)(*data - header);

	switch (pbuf->sec_algo) {
		case UMESH_L2_SECURITY_ALGO_NONE:
			/* Output length is the same as the input length, copy
			 * the data only. Check if the output buffer is big
			 * enough. */
			if (len > pbuf->size) {
				return UMESH_L2_PARSE_DATA_TOO_BIG;
			}

			pbuf->len = len;
			memcpy(pbuf->data, *data, len);
			(*data) += len;

			break;

		case UMESH_L2_SECURITY_ALGO_CRC16_CCITT: {
			/* Any packet smaller than 2 bytes is invalid - even the
			 * CRC alone cannot be contained in such packet. */
			if (len < 2) {
				return UMESH_L2_PARSE_DATA_NO_DATA;
			}

			/* Check if the destination buffer is big enough. */
			if ((len - 2) > pbuf->size) {
				return UMESH_L2_PARSE_DATA_TOO_BIG;
			}

			uint16_t crc_packet = (uint16_t)(((*data)[len - 2] << 8) | (*data)[len - 1]);
			uint16_t crc_computed = crc16(header, len - 2 + header_len);

			/* If the computed CRC and the CRC sent in the packet
			 * differs, return CRC error. */
			if (crc_packet != crc_computed) {
				return UMESH_L2_PARSE_DATA_AE_FAILED;
			}

			/* Copy the error-checked plaintext data. */
			pbuf->len = len - 2;
			memcpy(pbuf->data, *data, len - 2);
			(*data) += len;

			/* Everything is ok so far, set the packet security. */
			pbuf->sec_type = UMESH_L2_PBUF_SECURITY_VERIFY;

			break;
		}

		case UMESH_L2_SECURITY_ALGO_CHACHA20_POLY1305_2:
			tag_len = 4;
			/* No break! */
		case UMESH_L2_SECURITY_ALGO_CHACHA20_POLY1305_4: {
			if (tag_len == 0) {
				tag_len = 4;
			}

			if (len < (2 + tag_len)) {
				return UMESH_L2_PARSE_DATA_NO_DATA;
			}

			if (umesh_l2_parse_counter(data, &len, &(pbuf->counter)) != UMESH_L2_PARSE_COUNTER_OK) {
				return UMESH_L2_PARSE_DATA_FAILED;
			}

			/* Generate a key for Poly1305. Use Chacha20 counter value
			 * of zero for this purpose. Verify the data. */
			uint8_t poly1305_key[32];
			umesh_l2_get_ctr_keystream(poly1305_key, sizeof(poly1305_key), test_key, 32, pbuf->counter, 0, UMESH_L2_ENC_ALGO_CHACHA20);
			if (umesh_l2_data_authenticate(header, header_len + 2 + len - tag_len, poly1305_key, 16, *data + len - tag_len, tag_len, UMESH_L2_AUTH_ALGO_POLY1305) != UMESH_L2_DATA_AUTHENTICATE_OK) {
				return UMESH_L2_PARSE_DATA_AE_FAILED;
			}

			/* Everything is ok so far, set the packet security and
			 * decrypt the data.  */
			pbuf->sec_type = UMESH_L2_PBUF_SECURITY_AE;
			pbuf->len = len - tag_len;
			umesh_l2_decrypt(pbuf->data, pbuf->size, data, pbuf->len, test_key, 32, pbuf->counter, UMESH_L2_ENC_ALGO_CHACHA20);

			break;
		}

		default:
			/* The requested algorithm is not supported yet. */
			return UMESH_L2_PARSE_DATA_UNSUPPORTED;
	}

	/* Data was extracted/parsed/decrypted/authenticated successfully. */
	return UMESH_L2_PARSE_DATA_OK;
}


/**
 * Successfully parsed layer 2 packet buffers are passed to this single
 * function. It makes a routing decision on the received buffers. This means:
 *   * checking and manipulating DTID and STID values
 *
 * In this case the following applies:
 *   * if the packet has broadcast flag set, it passes the check
 *   * if the destination TID passed a check in the neighbor discovery module
 *     (ie. it is one of the TIDs advertised by the nbdisc module itself),
 *     the same apply
 *   * if the DTID is unknown, the packet is silently dropped
 *   * if the SDID is unknown (not in our neighbor table), the packet is dropped
 */
int32_t umesh_l2_receive_route_pbuf(struct module_umesh *umesh, struct umesh_l2_pbuf *pbuf) {
	if (u_assert(umesh != NULL && pbuf != NULL)) {
		return UMESH_L2_RECEIVE_ROUTE_PBUF_FAILED;
	}

	/* Check the non-broadcast packets. */
	if (!(pbuf->flags & UMESH_L2_PBUF_FLAG_BROADCAST)) {
		/* No DTID and not broadcast, invalid packet. */
		if (pbuf->dtid == 0) {
			return UMESH_L2_RECEIVE_ROUTE_PBUF_FAILED;
		}

		/* Valid DTID but not ours. */
		if (pbuf->dtid != umesh->nbdiscovery.tid_current &&
		    pbuf->dtid != umesh->nbdiscovery.tid_last) {
			return UMESH_L2_RECEIVE_ROUTE_PBUF_FAILED;
		}
	}

	/** @todo check stid (must be in the nbtable). */
	/** @todo ignore stid check if the protocol descriptor says so. */

	return UMESH_L2_RECEIVE_ROUTE_PBUF_OK;
}


