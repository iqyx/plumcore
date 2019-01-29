/*
 * rMAC packet buffer/encoder/decoder
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

#include "pb_encode.h"
#include "pb_decode.h"
#include "rmac_pbuf.pb.h"
#include "lib/crypto/blake2.h"


#define MODULE_NAME "rmac-pbuf"
#define ASSERT(c, r) if (u_assert(c)) return r


rmac_pbuf_ret_t rmac_pbuf_clear(RmacPbuf *self) {
	ASSERT(self != NULL, RMAC_PBUF_RET_NULL);

	memset(self, 0, sizeof(RmacPbuf));
	self->msg = (RmacPacket)RmacPacket_init_default;
	return RMAC_PBUF_RET_OK;
}


rmac_pbuf_ret_t rmac_pbuf_use(RmacPbuf *self) {
	ASSERT(self != NULL, RMAC_PBUF_RET_NULL);

	self->used = true;
	return RMAC_PBUF_RET_OK;
}


rmac_pbuf_ret_t rmac_pbuf_universe_key(RmacPbuf *self, uint8_t *key, size_t len) {
	ASSERT(self != NULL, RMAC_PBUF_RET_NULL);

	/* No length constraints for the key except it must be non-empty. */
	ASSERT(key != NULL, RMAC_PBUF_RET_BAD_ARG);
	ASSERT(len > 0, RMAC_PBUF_RET_BAD_ARG);

	/** @todo apply blake2 prf to the key and generate two 128bit keys
	 *        (encrypt key Ke and MAC key Km).
	 */
	ASSERT(RMAC_PBUF_KE_LEN + RMAC_PBUF_KM_LEN <= BLAKE2S_OUTBYTES, RMAC_PBUF_RET_FAILED);
	uint8_t res[RMAC_PBUF_KE_LEN + RMAC_PBUF_KM_LEN] = {0};
	blake2s(res, RMAC_PBUF_KE_LEN + RMAC_PBUF_KM_LEN, key, len, NULL, 0);

	/* Now split the key. */
	memcpy(self->key_encrypt, res, RMAC_PBUF_KE_LEN);
	memcpy(self->key_mac, res + RMAC_PBUF_KE_LEN, RMAC_PBUF_KM_LEN);

	return RMAC_PBUF_RET_OK;
}


/* The encryption is reversible. This single function can be used bot for
 * encryption and decryption. */
static rmac_pbuf_ret_t crypt_in_place(uint8_t *buf, size_t len, uint8_t siv[RMAC_PBUF_SIV_LEN], uint8_t key[RMAC_PBUF_KE_LEN]) {
	ASSERT(buf != NULL, RMAC_PBUF_RET_BAD_ARG);
	ASSERT(len > 0, RMAC_PBUF_RET_BAD_ARG);
	ASSERT(siv != NULL, RMAC_PBUF_RET_BAD_ARG);
	ASSERT(key != NULL, RMAC_PBUF_RET_BAD_ARG);

	/* A single byte block counter. */
	uint8_t i = 0;
	while (len > 0) {
		/* Generate a BLAKE2S_OUTBYTES of keystream. */
		uint8_t keystream[BLAKE2S_OUTBYTES] = {0};
		blake2s_state s;
		blake2s_init_key(&s, BLAKE2S_OUTBYTES, key, RMAC_PBUF_KE_LEN);
		blake2s_update(&s, siv, RMAC_PBUF_SIV_LEN);
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

	return RMAC_PBUF_RET_OK;
}


rmac_pbuf_ret_t rmac_pbuf_read(RmacPbuf *self, uint8_t *buf, size_t len) {
	ASSERT(self != NULL, RMAC_PBUF_RET_NULL);
	ASSERT(buf != NULL, RMAC_PBUF_RET_BAD_ARG);

	/* The packet must contain at least the SIV + 1 byte of data. */
	if (len < (RMAC_PBUF_SIV_LEN + 1)) {
		return RMAC_PBUF_RET_DECRYPT_FAILED;
	}

	if (len > RMAC_PBUF_PACKET_LEN_MAX) {
		return RMAC_PBUF_RET_DECRYPT_FAILED;
	}

	/* OK, we can continue, there is at least 16 byte SIV on the beginning.
	 * The rest is data to decrypt. */
	uint8_t *siv = buf;
	uint8_t *data = buf + RMAC_PBUF_SIV_LEN;
	size_t data_len = len - RMAC_PBUF_SIV_LEN;

	if (crypt_in_place(data, data_len, siv, self->key_encrypt) != RMAC_PBUF_RET_OK) {
		return RMAC_PBUF_RET_DECRYPT_FAILED;
	}

	/* Now compute H() of the received data and compare to the received SIV. */
	uint8_t mac_received[RMAC_PBUF_SIV_LEN] = {0};
	blake2s(mac_received, sizeof(mac_received), data, data_len, self->key_mac, RMAC_PBUF_KM_LEN);

	/** @todo constant-time compare */
	if (memcmp(siv, mac_received, RMAC_PBUF_SIV_LEN)) {
		/* Just to be sure nobody processes the fake data. */
		memset(data, 0, data_len);
		return RMAC_PBUF_RET_MAC_FAILED;
	}

	/* If the authentication succeeded, try to parse the data. */
	self->msg = (RmacPacket)RmacPacket_init_default;

	pb_istream_t istream;
        istream = pb_istream_from_buffer(data, data_len);
	if (!pb_decode(&istream, RmacPacket_fields, &self->msg)) {
		return RMAC_PBUF_RET_DECODING_FAILED;
	}

	return RMAC_PBUF_RET_OK;
}


rmac_pbuf_ret_t rmac_pbuf_write(RmacPbuf *self, uint8_t *buf, size_t buf_size, size_t *len) {
	ASSERT(self != NULL, RMAC_PBUF_RET_NULL);
	ASSERT(buf != NULL, RMAC_PBUF_RET_BAD_ARG);

	if (buf_size < (RMAC_PBUF_SIV_LEN + 1)) {
		return RMAC_PBUF_RET_ENCODING_FAILED;
	}
	/* Do not check the upper bound. */

	/* Generate a random padding to hide the actual packet length and
	 * enrich the SIV with some entropy. */
	/** @todo */

	/* SIV will be placed at the beginning, the data will follow. */
	uint8_t *siv = buf;
	uint8_t *data = buf + RMAC_PBUF_SIV_LEN;

	/* Serialize the protobuf message into a stream. Do now allow
	 * the buffer to overflow. */
	pb_ostream_t ostream;
        ostream = pb_ostream_from_buffer(data, buf_size - RMAC_PBUF_SIV_LEN);
        if (!pb_encode(&ostream, RmacPacket_fields, &self->msg)) {
		/* The packet did not fit into the buffer or some other
		 * error occurred. */
		return RMAC_PBUF_RET_ENCODING_FAILED;

	}
	size_t data_len = ostream.bytes_written;

	ASSERT(RMAC_PBUF_SIV_LEN + data_len <= buf_size, RMAC_PBUF_RET_ENCODING_FAILED);

	/* Data is serialized, compute the MAC (used as a SIV). */
	blake2s(siv, RMAC_PBUF_SIV_LEN, data, data_len, self->key_mac, RMAC_PBUF_KM_LEN);

	/* And finally encrypt the serialized packet in-place using the computed SIV. */
	if (crypt_in_place(data, data_len, siv, self->key_encrypt) != RMAC_PBUF_RET_OK) {
		return RMAC_PBUF_RET_ENCRYPT_FAILED;
	}

	*len = RMAC_PBUF_SIV_LEN + data_len;

	return RMAC_PBUF_RET_OK;
}


