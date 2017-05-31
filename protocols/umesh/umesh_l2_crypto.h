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

#ifndef _UMESH_L2_CRYPTO_H_
#define _UMESH_L2_CRYPTO_H_

#include <stdint.h>


int32_t umesh_l2_data_authenticate(const uint8_t *data, uint32_t data_len,
	const uint8_t *key, uint32_t key_len, const uint8_t *tag, uint32_t tag_len, enum umesh_l2_auth_algo algo);
#define UMESH_L2_DATA_AUTHENTICATE_OK 0
#define UMESH_L2_DATA_AUTHENTICATE_FAILED -1

int32_t umesh_l2_get_ctr_keystream(uint8_t *keystream, uint32_t keystream_len, const uint8_t *key, uint32_t key_len,
	uint32_t nonce, uint32_t counter, enum umesh_l2_enc_algo algo);
#define UMESH_L2_GET_CTR_KEYSTREAM_OK 0
#define UMESH_L2_GET_CTR_KEYSTREAM_FAILED -1

int32_t umesh_l2_decrypt(uint8_t *dst, uint32_t dst_len, const uint8_t **src, uint32_t src_len,
	const uint8_t *key, uint32_t key_len, uint32_t nonce, enum umesh_l2_enc_algo algo);
#define UMESH_L2_DECRYPT_OK 0
#define UMESH_L2_DECRYPT_FAILED -1

#endif

