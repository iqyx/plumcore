/**
 * uMeshFw uMesh send
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
#include "umesh_l2_send.h"
#include "umesh_l2_pbuf.h"
#include "umesh_l2_crypto.h"
#include "crc.h"
#include "poly1305.h"


static const uint8_t test_key[] = {
	0xb0, 0x03, 0x44, 0xb5, 0xbe, 0x2c, 0x1e, 0x3c, 0x91, 0x29, 0xfe, 0x27, 0x2d, 0x99, 0x10, 0x90,
	0x3f, 0xce, 0x32, 0xc5, 0x68, 0x16, 0x56, 0x37, 0xfe, 0xda, 0xa8, 0x53, 0x70, 0xae, 0xd1, 0x10
};

int32_t umesh_l2_send(struct module_umesh *umesh, struct umesh_l2_pbuf *pbuf) {
	if (u_assert(pbuf != NULL)) {
		return UMESH_L2_SEND_FAILED;
	}

	if (umesh->l2_debug_packet_tx) {
		u_log(system_log, LOG_TYPE_DEBUG,
			"%s: umesh_l2_send: sending packet, stid=%u dtid=%u",
			umesh->module.name,
			pbuf->stid,
			pbuf->dtid
		);
	}

	/* If this pointer is not-null, the neighbor which this particular packet
	 * is for is known. */
	struct umesh_l2_nbtable_item *nbtable_item = NULL;

	uint8_t sandbox[UMESH_L2_SANDBOX_SIZE];
	uint8_t *pos = sandbox;

	/* Route the packet first - the destination of the packet is determined
	 * (neighbor table is used). If the packet is destined to an unknown
	 * neighbor, routing fails and the packet is not sent. */
	if (umesh_l2_send_route_pbuf(umesh, pbuf) != UMESH_L2_SEND_ROUTE_PBUF_OK) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_send: unroutable packet, send failed", umesh->module.name);
		}
		return UMESH_L2_SEND_UNROUTABLE;
	}

	/* Match the neighbor neighbor. */
	const uint8_t *key = NULL;
	pbuf->counter = 0;
	if (umesh_l2_nbtable_find(&(umesh->nbtable), pbuf->dtid, &nbtable_item) == UMESH_L2_NBTABLE_FIND_OK) {
		umesh->l2_pbuf.known_neighbor = true;

		/** @todo */
		//~ key = nbtable_item->txkey;
		key = test_key;
		pbuf->counter = nbtable_item->txcounter;
		/** @todo this increments the counter also when no encryption
		 *        is enabled. */
		nbtable_item->txcounter++;
	}

	/* The L2 packet buffer must be valid and well-formed. Check for common
	 * mistakes which could be made while creating the L2 packet buffer.
	 * Besides other things, the packet must be correctly routed - ie. it
	 * must have either the broadcast flag set or the interface must be
	 * known. */
	if (umesh_l2_send_check(umesh, pbuf) != UMESH_L2_SEND_CHECK_OK) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_send: malformed pbuf", umesh->module.name);
		}
		return UMESH_L2_SEND_MALFORMED;
	}

	/* Now we have a valid L2 packet. */

	if (umesh_l2_build_control(pbuf, &pos, sizeof(sandbox) - (pos - sandbox)) != UMESH_L2_BUILD_CONTROL_OK) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_send: cannot build the packet control field", umesh->module.name);
		}
		goto err;
	}

	/* DTID is added to the packet only if the broadcast flag is not set. */
	if (!(pbuf->flags & UMESH_L2_PBUF_FLAG_BROADCAST)) {
		if (umesh_l2_build_tid(pbuf->dtid, &pos, sizeof(sandbox) - (pos - sandbox)) != UMESH_L2_BUILD_TID_OK) {
			if (umesh->l2_debug_packet_rx_error) {
				u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_send: cannot build DTID", umesh->module.name);
			}
			goto err;
		}
	}

	/* STID is always added. */
	if (umesh_l2_build_tid(pbuf->stid, &pos, sizeof(sandbox) - (pos - sandbox)) != UMESH_L2_BUILD_TID_OK) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_send: cannot build STID", umesh->module.name);
		}
		goto err;
	}

	/* And finally append the actual packet data. */
	if (umesh_l2_build_data(pbuf, sandbox, &pos, sizeof(sandbox) - (pos - sandbox), key, 32) != UMESH_L2_BUILD_DATA_OK) {
		if (umesh->l2_debug_packet_rx_error) {
			u_log(system_log, LOG_TYPE_DEBUG, "%s: umesh_l2_send: cannot build packet data", umesh->module.name);
		}
		goto err;
	}

	if (umesh->macs[pbuf->iface] != NULL) {
		if (interface_mac_send(umesh->macs[0], sandbox, pos - sandbox + 1) != INTERFACE_MAC_SEND_OK) {
			goto err;
		}
	}

	return UMESH_L2_SEND_OK;

err:
	return UMESH_L2_SEND_FAILED;
}


int32_t umesh_l2_send_check(struct module_umesh *umesh, struct umesh_l2_pbuf *pbuf) {
	if (u_assert(umesh != NULL && pbuf != NULL)) {
		return UMESH_L2_SEND_CHECK_FAILED;
	}

	/* Every assert is on single line for nicer output. */
	if (u_assert(pbuf->dtid != 0 || pbuf->flags & UMESH_L2_PBUF_FLAG_BROADCAST) ||
	    u_assert(pbuf->data != NULL) ||
	    u_assert(pbuf->len <= UMESH_L2_PACKET_SIZE) ||
	    u_assert(pbuf->stid != 0)) {
		return UMESH_L2_SEND_CHECK_FAILED;
	}

	/** @todo security type and layer3 proto checking. */

	return UMESH_L2_SEND_CHECK_OK;
}


int32_t umesh_l2_build_control(struct umesh_l2_pbuf *pbuf, uint8_t **data, uint32_t len) {
	if (u_assert(pbuf != NULL && data != NULL && *data != NULL)) {
		return UMESH_L2_BUILD_CONTROL_FAILED;
	}

	uint8_t byte1 = 0;
	uint8_t byte2 = 0;

	/* Add the broadcast flag. */
	if (pbuf->flags & UMESH_L2_PBUF_FLAG_BROADCAST) {
		byte1 |= 0x40;
	}

	/* Add Enc1 and Enc0 bits to the first byte and the Enc2 bit to the
	 * second byte. Bit positions are described in the
	 * umesh_l2_parse_control function. Values greater than 7 are saved
	 * as modulo 8. */
	byte1 |= (uint8_t)((pbuf->sec_algo & 0x03) << 4);
	byte2 |= (uint8_t)(((pbuf->sec_algo >> 2) & 0x01) << 6);

	/* Add layer3 protocol bits L3p1 and L3p0 to the first control byte and
	 * bits L3p3 and L3p2 to the second control byte. Values greater than
	 * 15 are saved as modulo 16. */
	byte1 |= (uint8_t)((pbuf->proto & 0x03) << 2);
	byte2 |= (uint8_t)((pbuf->proto >> 2) & 0x03);

	/* If the second control byte is not zero, set the Ext bit in the first
	 * byte. */
	if (byte2 != 0) {
		byte1 |= 0x80;
	}

	/* We are going to save the first control byte. Check if there is
	 * required space available. Return otherwise. */
	if (len < 1) {
		return UMESH_L2_BUILD_CONTROL_BUFFER_SMALL;
	}
	**data = byte1;
	(*data)++;
	len--;

	/* And the second byte if it is nonzero. */
	/* and save byte2 if not null */
	if (byte2 != 0) {
		if (len < 1) {
			return UMESH_L2_BUILD_CONTROL_BUFFER_SMALL;
		}
		**data = byte2;
		(*data)++;
		len--;
	}

	return UMESH_L2_BUILD_CONTROL_OK;
}


int32_t umesh_l2_build_tid(uint32_t tid, uint8_t **data, uint32_t len) {
	if (u_assert(data != NULL && *data != NULL)) {
		return UMESH_L2_BUILD_TID_FAILED;
	}

	/* Start at the first chunk of 7bit data (the one shifted 28bits to
	 * the left. */
	int8_t i = 4;

	/* Consume zero parts first. Leave the last 7bit chunk even if zero. */
	while (((tid >> (i * 7)) & 0x7f) == 0 && i > 0) {
		i--;
	}

	/* And emit TID bytes until the last 7bit chunk is processed. */
	while (i >= 0) {
		if (len < 1) {
			return UMESH_L2_BUILD_TID_BUFFER_SMALL;
		} else {
			**data = (uint8_t)((tid >> (i * 7)) & 0x7f);

			/* If this is not the last byte, set the Ext bit. */
			if (i != 0) {
				**data |= (uint8_t)0x80;
			}

			i--;
			len--;
			(*data)++;
		}
	}

	return UMESH_L2_BUILD_TID_OK;
}


int32_t umesh_l2_build_data(struct umesh_l2_pbuf *pbuf, const uint8_t *header, uint8_t **data, uint32_t len, uint8_t *key, uint32_t key_len) {
	if (u_assert(pbuf != NULL && header != NULL && data != NULL && *data != NULL)) {
		return UMESH_L2_BUILD_DATA_FAILED;
	}

	/* Header length is the difference between start of the data and the
	 * header pointer. It is used for CRC computation and packet
	 * authentication. */
	uint32_t header_len = (uint8_t)(*data - header);

	/* Temporary variable holding the length of the authentication tag. */
	uint32_t tag_len = 0;

	switch (pbuf->sec_algo) {
		case UMESH_L2_SECURITY_ALGO_NONE:
			/* Output data length is the same as the data contained
			 * in the packet buffer. Check if there is enough space
			 * available. */
			if (pbuf->len > len) {
				return UMESH_L2_BUILD_DATA_BUFFER_SMALL;
			}

			memcpy(*data, pbuf->data, pbuf->len);
			(*data) += pbuf->len;
			len -= pbuf->len;

			break;

		case UMESH_L2_SECURITY_ALGO_CRC16_CCITT: {
			/* If the output buffer size is smaller than 2 bytes,
			 * the buffer cannot be used regardless of the packet
			 * data size. */
			if (len < 2) {
				return UMESH_L2_BUILD_DATA_BUFFER_SMALL;
			}

			/* Check if the destination buffer is big enough. */
			if ((pbuf->len + 2) > len) {
				return UMESH_L2_BUILD_DATA_BUFFER_SMALL;
			}

			/* Copy the plaintext data first. */
			memcpy(*data, pbuf->data, pbuf->len);
			(*data) += pbuf->len;
			len -= pbuf->len;

			/* Check if there is enough space for the CRC16. */
			if (len < 2) {
				return UMESH_L2_BUILD_DATA_BUFFER_SMALL;
			}

			uint16_t crc = crc16(header, header_len + pbuf->len);

			/* Append the computed CRC. */
			**data = crc >> 8;
			(*data)++;
			**data = crc & 0xff;
			(*data)++;
			len -= 2;

			break;
		}

		case UMESH_L2_SECURITY_ALGO_CHACHA20_POLY1305_2:
			tag_len = 4;
			/* No break! */
		case UMESH_L2_SECURITY_ALGO_CHACHA20_POLY1305_4: {
			if (tag_len == 0) {
				tag_len = 4;
			}

			/* Minimum required size is the length of the IV/nonce
			 * (16 bit counter value), the auth tag size + data size. */
			if (len < (2 + tag_len + pbuf->len)) {
				return UMESH_L2_BUILD_DATA_BUFFER_SMALL;
			}

			if (key == NULL || (key_len != 16 && key_len != 32)) {
				return UMESH_L2_BUILD_DATA_FAILED;
			}

			/* Add the counter (chacha20 nonce), 16 bits. */
			**data = pbuf->counter >> 8;
			(*data)++;
			**data = pbuf->counter & 0xff;
			(*data)++;
			len -= 2;

			uint32_t remaining = pbuf->len;
			uint8_t *pos = pbuf->data;
			uint16_t counter = 1;

			/* Add data in blocks of 64 bytes (chacha20 block size). */
			while (remaining > 0) {

				uint8_t keystream[64];
				umesh_l2_get_ctr_keystream(keystream, sizeof(keystream), test_key, 32, pbuf->counter, counter, UMESH_L2_ENC_ALGO_CHACHA20);
				counter++;

				/* We are processing in 64 byte blocks if there is
				 * enough data. Process the tail as a smaller block. */
				uint32_t bytes = 64;
				if (remaining < 64) {
					bytes = remaining;
				}

				/* XOR the original data from the packet buffer with
				 * the generated keystream. Advance to the next block. */
				for (uint32_t i = 0; i < bytes; i++) {
					**data = *pos ^ keystream[i];
					(*data)++;
					pos++;
				}
				remaining -= bytes;
			}


			uint8_t poly1305_key[32];
			umesh_l2_get_ctr_keystream(poly1305_key, sizeof(poly1305_key), test_key, 32, pbuf->counter, 0, UMESH_L2_ENC_ALGO_CHACHA20);

			/* Compute the Poly1305 authenticating tag. */
			uint8_t poly1305_tag[16];
			poly1305(poly1305_tag, header, header_len + 2 + pbuf->len, poly1305_key);
			//~ u_log(system_log, LOG_TYPE_DEBUG, "tx poly len = %u", header_len + 2 + pbuf->len);

			/* And append only truncated part of the tag. */
			memcpy(*data, poly1305_tag, tag_len);
			(*data) += tag_len;

			break;
		}

		default:
			/* The requested algorithm is not supported yet. */
			return UMESH_L2_BUILD_DATA_UNSUPPORTED;
	}

	/* The data part of the packet was built successfully and the error
	 * correction/authentication tag was added (if requested). */
	return UMESH_L2_BUILD_DATA_OK;
}


/**
 * Packets which should be sent are routed first. Routing a packet means:
 *   * checking and manipulating DTID and STID values
 *   * changing the output interface
 *
 * In this case the following applies:
 *   * STID value of the packet is set to our actual valid TID value
 *   * DTIT is checked against the neighbor table if the broadcast flag is not
 *     set. If the DTID is found, destination interface is determined and the
 *     packet is sent.
 *   * if the broadcast flag is set, the packet is sent.
 */
int32_t umesh_l2_send_route_pbuf(struct module_umesh *umesh, struct umesh_l2_pbuf *pbuf) {
	if (u_assert(umesh != NULL && pbuf != NULL)) {
		return UMESH_L2_SEND_ROUTE_PBUF_FAILED;
	}

	/* The particular security algorithm is choosen according to the
	 * neighbor table (neighbors advertise which algos they are
	 * accepting). */
	switch (pbuf->sec_type) {
		case UMESH_L2_PBUF_SECURITY_NONE:
			pbuf->sec_algo = UMESH_L2_SECURITY_ALGO_NONE;
			break;

		case UMESH_L2_PBUF_SECURITY_VERIFY:
			/** @todo This should be configurable. */
			pbuf->sec_algo = UMESH_L2_SECURITY_ALGO_CRC16_CCITT;
			break;

		case UMESH_L2_PBUF_SECURITY_AE:
			/** @todo This should be configurable. */
			pbuf->sec_algo = UMESH_L2_SECURITY_ALGO_CHACHA20_POLY1305_2;
			break;

		default:
			/* Unknown security type selected, don't route the
			 * packet. */
			return UMESH_L2_SEND_ROUTE_PBUF_FAILED;
	};

	/* Set STID to the value provided by the nbdiscovery module. */
	if (pbuf->stid == 0) {
		pbuf->stid = umesh->nbdiscovery.tid_current;
	}
	if (pbuf->stid == 0) {
		/* No valid TID, return error. */
		return UMESH_L2_SEND_ROUTE_PBUF_FAILED;
	}

	pbuf->iface = 0;

	/** @todo set output interface (lookup in the nbtable) */

	return UMESH_L2_SEND_ROUTE_PBUF_OK;
}

