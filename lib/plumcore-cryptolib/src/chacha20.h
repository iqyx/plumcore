/* SPDX-License-Identifier: CC0-1.0
 *
 * ChaCha20 sanitized implementation
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 *
 * Based on code by
 * chacha-ref.c version 20080118, D. J. Bernstein, Public domain.
 */

#pragma once

typedef struct chacha20_context {
	uint32_t input[16];

} ChaCha20;


void chacha20_encrypt(ChaCha20 *ctx, const uint8_t *m, uint8_t *c, size_t len);
void chacha20_keysetup(ChaCha20 *ctx, const uint8_t *k, uint32_t kbits);
void chacha20_nonce(ChaCha20 *ctx, const uint8_t nonce[8]);

