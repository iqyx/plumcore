/**
 * uMeshFw uMesh layer 2 crypto functions
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

#include "u_assert.h"
#include "u_log.h"
#include "umesh_l2_pbuf.h"
#include "umesh_l2_crypto.h"
#include "crc.h"
#include "chacha20.h"
#include "poly1305.h"


int32_t umesh_l2_data_authenticate(const uint8_t *data, uint32_t data_len, const uint8_t *key, uint32_t key_len, const uint8_t *tag, uint32_t tag_len, enum umesh_l2_auth_algo algo) {

	if (algo == UMESH_L2_AUTH_ALGO_POLY1305) {
		/* Check parameters for Poly1305. */
		if (u_assert(key_len >= 16 && tag_len > 0 && tag_len <= 16)) {
			return UMESH_L2_DATA_AUTHENTICATE_FAILED;
		}

		/* Compute the Poly1305 authenticating tag. */
		uint8_t poly1305_tag[16];
		poly1305(poly1305_tag, data, data_len, key);

		//~ u_log(system_log, LOG_TYPE_DEBUG, "rx poly len = %u", header_len + len - tag_len);

		if (memcmp(poly1305_tag, tag, tag_len)) {
			//~ u_log(system_log, LOG_TYPE_DEBUG, "compare %02x%02x%02x%02x != %02x%02x%02x%02x",
				//~ poly1305_tag[0],
				//~ poly1305_tag[1],
				//~ poly1305_tag[2],
				//~ poly1305_tag[3],
				//~ tag[0],
				//~ tag[1],
				//~ tag[2],
				//~ tag[3]
			//~ );
			return UMESH_L2_DATA_AUTHENTICATE_FAILED;
		}
		//~ u_log(system_log, LOG_TYPE_DEBUG, "compare ok, tag_len = %u", tag_len);
		return UMESH_L2_DATA_AUTHENTICATE_OK;
	}

	/* No authentication algorithm matched. */
	return UMESH_L2_DATA_AUTHENTICATE_FAILED;
}


int32_t umesh_l2_get_ctr_keystream(uint8_t *keystream, uint32_t keystream_len, const uint8_t *key, uint32_t key_len, uint32_t nonce, uint32_t counter, enum umesh_l2_enc_algo algo) {

	if (algo == UMESH_L2_ENC_ALGO_CHACHA20) {
		if (u_assert(key_len == 16 || key_len == 32) ||
		    u_assert(keystream_len <= 64)) {
			return UMESH_L2_GET_CTR_KEYSTREAM_FAILED;
		}

		struct chacha20_context_t ctx;

		chacha20_keysetup(&ctx, key, key_len * 8);
		chacha20_nonce(&ctx, (uint8_t[]) {
			(nonce >> 24) & 0xff,
			(nonce >> 16) & 0xff,
			(nonce >> 8) & 0xff,
			nonce & 0xff,
			0,
			0,
			0,
			0
		});
		chacha20_counter(&ctx, counter);
		uint8_t k[64];
		chacha20_keystream(&ctx, k);

		memcpy(keystream, k, keystream_len);

		return UMESH_L2_GET_CTR_KEYSTREAM_OK;
	}

	return UMESH_L2_GET_CTR_KEYSTREAM_FAILED;
}


int32_t umesh_l2_decrypt(uint8_t *dst, uint32_t dst_len, const uint8_t **src, uint32_t src_len, const uint8_t *key, uint32_t key_len, uint32_t nonce, enum umesh_l2_enc_algo algo) {
	if (src_len > dst_len || nonce == 0) {
		return UMESH_L2_DECRYPT_FAILED;
	}

	if (algo == UMESH_L2_ENC_ALGO_CHACHA20) {

		uint16_t counter = 1;
		while (src_len > 0) {

			uint8_t keystream[64];
			umesh_l2_get_ctr_keystream(keystream, sizeof(keystream), key, key_len, nonce, counter, algo);
			counter++;

			/* We are processing in 64 byte blocks if there is
			 * enough data. Process the tail as a smaller block. */
			uint32_t bytes = 64;
			if (src_len < 64) {
				bytes = src_len;
			}

			/* XOR the original data from the packet buffer with
			 * the generated keystream. Advance to the next block. */
			for (uint32_t i = 0; i < bytes; i++) {
				*dst = **src ^ keystream[i];
				dst++;
				(*src)++;
			}
			src_len -= bytes;
		}
		return UMESH_L2_DECRYPT_OK;
	}

	return UMESH_L2_DECRYPT_FAILED;
}

