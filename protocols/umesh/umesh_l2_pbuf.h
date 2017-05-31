/**
 * uMeshFw uMesh layer 2 packet buffer implementation
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

#ifndef _UMESH_L2_PBUF_H_
#define _UMESH_L2_PBUF_H_

#include <stdint.h>
#include <stdbool.h>


/**
 * Maximum size of Layer3 data in the Layer2 packet.
 */
#define UMESH_L2_PACKET_SIZE 120

/**
 * Size of the sandbox area where the packets are being prepared for
 * transmission (passing to the MAC interface). It should be bigger than the
 * L2 packet size incremented by the maximum header length and the overhead
 * caused by encryption/authentication.
 *
 * Maximum header length is 2B (control) + 5B (DTID) + 5B (STID).
 * Maximum crypto overhead is 1B (counter) + 4B (auth tag).
 *
 * 20B overhead gives you a plenty of headroom. Usual situation will be
 * 1B (control) + 2B (dtid) + 2B (stid) + 1B (counter) + 2B (auth tag).
 */
#define UMESH_L2_SANDBOX_SIZE 140

/**
 * @section l2-ae-modes Layer2 encryption and authentication modes
 *
 * There are basically 3 types of encryption, authentication and error checking
 * supported:
 *
 *   - no encryption and no error detection - used in occasions when no keys
 *     have been established yet or the other party doesn't support any
 *     encryption, packet authentication nor error checking. There SHOULD be a
 *     configuration option to disable reception of such frames
 *
 *   - error detection only - used during the first phases of the key exchange
 *     when no keys have been established yet. Can be optionally used for
 *     communication between nodes which don't support any form of leyer 2
 *     encryption or for protocols which don't need to be secure in any way.
 *     In this mode, an error detection tag (checksum) is added at the end of
 *     the data. Its length depends on the choosen algorithm. There SHOULD be a
 *     configuration option to disable reception of such frames except during
 *     the key exchange phase.
 *
 *   - encryption and authentication - the whole communication is encrypted and
 *     authenticated using the selected cipher/method with the current ephemeral
 *     symmetric keys. A packet counter is needed to allow encryption using
 *     stream ciphers or block ciphers in CTR mode. This packet counter is
 *     prepended before the actual data and is monotonically increased for each
 *     packet (possibly overflowing). Communicating nodes must maintain their
 *     own counters which cannot overflow during the whole key exchange interval
 *     and which are adjusted using the packet counter. Ecrypted data follows
 *     (it has the same length as the original cleartext data). Authentication
 *     tag is then appended after the encrypted payload. Its size depends on the
 *     authentication algorithm used.
 *
 * In addition to methods provided by layer 2 protocol, another methods for
 * error detection and correction may be used directly in in the MAC layer.
 */

/**
 * Packet security type is specified by this single enum for both directions -
 * it indicates which method should be used when sending the packet or it
 * indicates if and how was the received packet checked/authenticated during
 * reception.
 */
enum umesh_l2_pbuf_security_type {
	/**
	 * Sending - no security and data verification method is requested. No
	 * additional data is added when the packet is sent.
	 * Receiving - it means that the packet had no security specified by the
	 * sender or that the security check failed. Simply - it means that the
	 * packet cannot be treated as secure and its integrity is unknwon.
	 */
	UMESH_L2_PBUF_SECURITY_NONE,

	/**
	 * Sending - the sender requests that a tag for packet integrity
	 * verification is added.
	 * Receiving - the received packet has been checked and its integrity is
	 * verified. However, the contents of the packet were not encrypted.
	 */
	UMESH_L2_PBUF_SECURITY_VERIFY,

	/**
	 * Sending - authentication and encryption is requested.
	 * Receiving - the received packet was decrypted and authenticated
	 * using the current key.
	 */
	UMESH_L2_PBUF_SECURITY_AE,
};

/**
 * Different error detection, error correction, data encryption and data
 * authentication methods allowed on layer 2.
 */
enum umesh_l2_security_algo {
	/**
	 * No encryption, no data authentication, no error checking/detection,
	 * no error correction. No additional data is added/removed during the
	 * encryption/decryption.
	 */
	UMESH_L2_SECURITY_ALGO_NONE = 0,

	/**
	 * 2 byte error checking tag is added at the end of the original data.
	 * It is computed as a CRC16 using the CCITT polynomial (0x8110).
	 *
	 * Format of the packet data:
	 *
	 * +------------------------------------------------+-----------+
	 * | H bytes     | N bytes                          | 2 bytes   |
	 * +------------------------------------------------+-----------+
	 * | header      | data                             | CRC16     |
	 * +------------------------------------------------+-----------+
	 *  \______________________________________________/
	 *            source for CRC16 computation
	 *                \________________________________/
	 *                   length of the resulting data
	 */
	UMESH_L2_SECURITY_ALGO_CRC16_CCITT = 1,
	UMESH_L2_SECURITY_ALGO_CHACHA20_POLY1305_2 = 2,

	/**
	 * AES-128-CTR encryption with HMAC-SHA256 authentication.
	 * Initialization vector (IV) for the encryption is prepended before the
	 * actual data as a 2 byte value. The IV MUST NOT be repeated twice for
	 * the same encryption key. 2 byte IV is generated using a counter
	 * starting from the value 0. Maximum of 65536 different packets can be
	 * sent using the same AES key (new key exchange process is required
	 * to compute a new ephemeral key).
	 * HMAC is applied after the encryption. The whole packet is used as a
	 * source for the HMAC (including the packet header and IV). The result
	 * is then truncated to 2 bytes only and appended at the end.
	 *
	 * Format of the packet data:
	 *
	 * +------------------------------------------------+-----------+
	 * | H bytes     | 2 bytes | N bytes                | 2 bytes   |
	 * +------------------------------------------------+-----------+
	 * | header      | IV      | data                   | auth tag  |
	 * +------------------------------------------------+-----------+
	 *  \______________________________________________/
	 *            source for HMAC computation
	 *                          \______________________/
	 *                        length of the resulting data
	 */
	UMESH_L2_SECURITY_ALGO_AES128_HMAC_SHA256_2 = 3,
	UMESH_L2_SECURITY_ALGO_CRC32 = 4,
	UMESH_L2_SECURITY_ALGO_CHACHA20_POLY1305_4 = 5,
	UMESH_L2_SECURITY_ALGO_AES128_HMAC_SHA256_4 = 6,
};


enum umesh_l2_auth_algo {
	UMESH_L2_AUTH_ALGO_NONE = 0,
	UMESH_L2_AUTH_ALGO_POLY1305,
	UMESH_L2_AUTH_ALGO_HMAC_SHA256,
};

enum umesh_l2_enc_algo {
	UMESH_L2_ENC_ALGO_NONE = 0,
	UMESH_L2_ENC_ALGO_AES128,
	UMESH_L2_ENC_ALGO_CHACHA20,
};


/* packet buffer flags */
#define UMESH_L2_PBUF_FLAG_USED 1
#define UMESH_L2_PBUF_FLAG_BROADCAST 2

/* L2 packet buffer structure used for packet tx and rx */
struct umesh_l2_pbuf {
	/* destination and source TID of the packet */
	uint32_t dtid, stid;

	/* pbuf flags */
	uint8_t flags;

	/* packet security */
	enum umesh_l2_pbuf_security_type sec_type;
	enum umesh_l2_security_algo sec_algo;
	uint16_t counter;

	/* L3 payload type (protocol) */
	uint8_t proto;

	/* packet transmission priority */
	uint8_t prio;

	/* allocated packet buffer data & size */
	uint8_t *data;
	size_t size;

	/* real layer 3 raw data length */
	uint32_t len;

	/* interface index */
	uint32_t iface;

	/* packet buffer timeout */
	uint32_t timeout;

	int32_t rssi;
	int32_t bit_errors;
	int32_t fei;

	bool known_neighbor;
};



int32_t umesh_l2_pbuf_init(struct umesh_l2_pbuf *pbuf);
#define UMESH_L2_PBUF_INIT_OK 0
#define UMESH_L2_PBUF_INIT_FAILED -1

int32_t umesh_l2_pbuf_clear(struct umesh_l2_pbuf *pbuf);
#define UMESH_L2_PBUF_CLEAR_OK 0
#define UMESH_L2_PBUF_CLEAR_FAILED -1

int32_t umesh_l2_pbuf_free(struct umesh_l2_pbuf *pbuf);
#define UMESH_L2_PBUF_FREE_OK 0
#define UMESH_L2_PBUF_FREE_FAILED -1

#endif
