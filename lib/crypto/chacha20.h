/**
 * ChaCha20 implementation
 * By Marek Koza, qyx@krtko.org
 * 
 * Based on code by
 * chacha-ref.c version 20080118, D. J. Bernstein, Public domain.
 *
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 * 
 * This file is part of uMesh node firmware (http://qyx.krtko.org/embedded/umesh)
 *
 */

#ifndef _CHACHA20_H_
#define _CHACHA20_H_

typedef struct chacha20_context_t {
	uint32_t input[16];

} chacha20_context;


void chacha20_keystream(chacha20_context *ctx, uint8_t output[64]);
void chacha20_keysetup(chacha20_context *ctx, const uint8_t *k, uint32_t kbits);
void chacha20_nonce(chacha20_context *ctx, const uint8_t *nonce);
void chacha20_counter(chacha20_context *ctx, uint32_t counter);

#endif
