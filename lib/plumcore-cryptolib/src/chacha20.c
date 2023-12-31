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


#include <stdio.h>
#include <inttypes.h>
#include "ecrypt-portable.h"
#include "chacha20.h"

#define ROTATE(v, c) (ROTL32(v, c))
#define XOR(v, w) ((v) ^ (w))
#define PLUS(v, w) (U32V((v) + (w)))
#define PLUSONE(v) (PLUS((v), 1))

#define QUARTERROUND(a, b, c, d) \
x[a] = PLUS(x[a], x[b]); x[d] = ROTATE(XOR(x[d], x[a]), 16); \
x[c] = PLUS(x[c], x[d]); x[b] = ROTATE(XOR(x[b], x[c]), 12); \
x[a] = PLUS(x[a], x[b]); x[d] = ROTATE(XOR(x[d], x[a]), 8); \
x[c] = PLUS(x[c], x[d]); x[b] = ROTATE(XOR(x[b], x[c]), 7);

void chacha20_keystream(chacha20_context *ctx, uint8_t output[64]) {
	uint32_t x[16];
	int i;

	for (i = 0; i < 16; ++i) {
		x[i] = ctx->input[i];
	}
	
	for (i = 20; i > 0; i -= 2) {
		QUARTERROUND( 0,  4,  8, 12)
		QUARTERROUND( 1,  5,  9, 13)
		QUARTERROUND( 2,  6, 10, 14)
		QUARTERROUND( 3,  7, 11, 15)
		QUARTERROUND( 0,  5, 10, 15)
		QUARTERROUND( 1,  6, 11, 12)
		QUARTERROUND( 2,  7,  8, 13)
		QUARTERROUND( 3,  4,  9, 14)
	}
	
	for (i = 0; i < 16; ++i) {
		x[i] = PLUS(x[i], ctx->input[i]);
	}
	
	for (i = 0; i < 16; ++i) {
		U32TO8_LITTLE(output + 4 * i, x[i]);
	}
}

static const char sigma[16] = "expand 32-byte k";
static const char tau[16] = "expand 16-byte k";

void chacha20_keysetup(chacha20_context *ctx, const uint8_t *k, uint32_t kbits) {
	const char *constants;

	ctx->input[4] = U8TO32_LITTLE(k + 0);
	ctx->input[5] = U8TO32_LITTLE(k + 4);
	ctx->input[6] = U8TO32_LITTLE(k + 8);
	ctx->input[7] = U8TO32_LITTLE(k + 12);
	if (kbits == 256) { /* recommended */
		k += 16;
		constants = sigma;
	} else { /* kbits == 128 */
		constants = tau;
	}
	ctx->input[8] = U8TO32_LITTLE(k + 0);
	ctx->input[9] = U8TO32_LITTLE(k + 4);
	ctx->input[10] = U8TO32_LITTLE(k + 8);
	ctx->input[11] = U8TO32_LITTLE(k + 12);
	ctx->input[0] = U8TO32_LITTLE(constants + 0);
	ctx->input[1] = U8TO32_LITTLE(constants + 4);
	ctx->input[2] = U8TO32_LITTLE(constants + 8);
	ctx->input[3] = U8TO32_LITTLE(constants + 12);
}

void chacha20_nonce(chacha20_context *ctx, const uint8_t *nonce) {
	ctx->input[12] = 0;
	ctx->input[13] = 0;
	ctx->input[14] = U8TO32_LITTLE(nonce + 0);
	ctx->input[15] = U8TO32_LITTLE(nonce + 4);
}

void chacha20_counter(chacha20_context *ctx, uint32_t counter) {
	ctx->input[12] = counter;
	ctx->input[13] = 0;
}

/**
 * example usage:

	chacha20_keysetup(&c, key3, 256);
	chacha20_nonce(&c, nonce4);

	chacha20_counter(&c, 0);
	chacha20_keystream(&c, output);

*/
