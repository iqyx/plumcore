/* SPDX-License-Identifier: BSD-3-Clause
 *
 * A simple blake2s Reference Implementation (RFC7693)
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * Copyright (c) 2025 IETF Trust and the persons identified as authors of the code.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, is permitted pursuant to,
 * and subject to the license terms contained in, the Simplified BSD License set forth in Section 4.c of the
 * IETF Trust’s Legal Provisions Relating to IETF Documents (http://trustee.ietf.org/license-info).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
	uint8_t b[64];                      // input buffer
	uint32_t h[8];                      // chained state
	uint32_t t[2];                      // total number of bytes
	size_t c;                           // pointer for b[]
	size_t outlen;                      // digest size
} blake2s_ctx;
typedef blake2s_ctx blake2s_state;

int blake2s_init(blake2s_ctx *ctx, size_t outlen);
// Initialize the hashing context "ctx" with optional key "key".
//      1 <= outlen <= 32 gives the digest size in bytes.
//      Secret key (also <= 32 bytes) is optional (keylen = 0).
int blake2s_init_key(blake2s_ctx *ctx, size_t outlen, const void *key, size_t keylen);

// Add "inlen" bytes from "in" into the hash.
void blake2s_update(blake2s_ctx *ctx, const void *in, size_t inlen);

// Generate the message digest (size given in init).
//      Result placed in "out".
void blake2s_final(blake2s_ctx *ctx, void *out);

// All-in-one convenience function.
int blake2s(void *out, size_t outlen, const void *key, size_t keylen, const void *in, size_t inlen);

