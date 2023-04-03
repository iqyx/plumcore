/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Blake2s-SIV implementation
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <main.h>
#include <blake2.h>

#include "blake2s-siv.h"

/**
 * @brief A SIV-mode authenticated encryption based on the Blake2s PRF
 *
 * The idea comes from https://web.cs.ucdavis.edu/~rogaway/papers/siv.pdf:
 * - the input is a key, header and a plaintext
 * - output of the first phase is a MAC, serving as a SIV (synthetic IV)
 * - output of the second phase is a ciphertext using a key and the SIV as an IV
 *
 * Using the SIV mode in this use case have the advantage of not requiring
 * keeping a message counter value between reboots. In an embedded device this
 * could be a significant challenge. We also save some bytes by not requiring
 * and explicit IV.
 *
 * Although the original paper doesn't use Blake2s, we made this change
 * considering the following:
 *
 * - only a single PRF is used to do a key derivation (to get an encryption key Ke
 *   and a MAC key Km from a single shared key), a MAC (a keyed Blake2s can be
 *   used as a MAC, even without the HMAC construct) and encryption (Blake2s
 *   is used in a stream cipher mode where B2S(Ke | counter) is used to generate
 *   a cipherstream). This reduces code and memory footprint.
 * - it serves as an "obstruction layer" on layer 2/3. A proper encryption with
 *   a key exchange is done on higher layers. We may tolerate some fuckups here.
 * - don't roll your own crypto, huh!
 *
 * encryption:
 *
 * input key is K, input message is M, ciphertext is C
 * derive Ke, Km as (Ke | Km) = H(K)
 * compute a MAC serving as a IV: SIV = H(Km, M)
 * construct cipherstream as i:0->n, Cs = H(Ke, SIV | (i as a big endian uint32)) | H(..) | H(..)
 * encrypt C = M^Cs | SIV
 *
 * decryption:
 *
 * input key is K, ciphertext is C, output message is M
 * derive Ke, Km as (Ke | Km) = H(K)
 * construct cipherstream as i:0->n, Cs = H(Ke, SIV | (i as a big endian uint32)) | H(..) | H(..)
 * split the input Me | SIV = C
 * decrypt the message M = Me^Cs
 * validate MAC H(Km, M) == SIV?, return M if matches
 */


void b2s_derive_keys(const uint8_t *key, size_t len, uint32_t ke[B2S_KE_LEN], uint32_t km[B2S_KM_LEN]) {
	/* No length constraints for the key except it must be non-empty. */
	u_assert(key != NULL);
	u_assert(len > 0);
	u_assert(B2S_KE_LEN + B2S_KM_LEN <= BLAKE2S_OUTBYTES);

	uint8_t res[B2S_KE_LEN + B2S_KM_LEN] = {0};
	blake2s(res, B2S_KE_LEN + B2S_KM_LEN, key, len, NULL, 0);

	/* Now split the key. */
	memcpy(ke, res, B2S_KE_LEN);
	memcpy(km, res + B2S_KE_LEN, B2S_KM_LEN);
}


void b2s_crypt(uint8_t *buf, size_t len, const uint8_t *siv, size_t siv_len, const uint8_t ke[B2S_KE_LEN]) {
	u_assert(buf != NULL);
	u_assert(len > 0);
	u_assert(siv != NULL);
	u_assert(siv_len >= 4);
	u_assert(ke != NULL);
	u_assert(len <= (BLAKE2S_OUTBYTES * UINT8_MAX));

	/* Block counter */
	uint8_t i = 0;
	while (len > 0) {
		/* Generate a BLAKE2S_OUTBYTES of keystream. */
		uint8_t keystream[BLAKE2S_OUTBYTES] = {0};
		blake2s_state s;
		blake2s_init_key(&s, BLAKE2S_OUTBYTES, ke, B2S_KE_LEN);
		blake2s_update(&s, siv, siv_len);
		uint8_t b[4] = {0, 0, 0, i};
		blake2s_update(&s, b, sizeof(b));
		blake2s_final(&s, keystream, BLAKE2S_OUTBYTES);

		/* Crunch BLAKE2S_OUTBYTES or less in one step. */
		size_t block_len = len;
		if (block_len > BLAKE2S_OUTBYTES) {
			block_len = BLAKE2S_OUTBYTES;
		}
		for (size_t j = 0; j < block_len; j++) {
			buf[j] = buf[j] ^ keystream[j];
		}

		/* Advance to the next block. */
		buf += block_len;
		len -= block_len;
		i++;
	}
	/* Now len = 0 and buf is at the end. */
}


void b2s_siv(const uint8_t *buf, size_t len, uint8_t *siv, size_t siv_len, const uint8_t km[B2S_KM_LEN]) {
	u_assert(buf != NULL);
	u_assert(len > 0);
	u_assert(siv != NULL);
	u_assert(siv_len >= 4);
	u_assert(km != NULL);

	blake2s_state s;
	blake2s_init_key(&s, BLAKE2S_OUTBYTES, km, B2S_KE_LEN);
	blake2s_update(&s, buf, len);
	blake2s_final(&s, siv, siv_len);
}
