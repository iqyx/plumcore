/**
 * pseudorandom number generator
 *
 * Copyright (C) 2014, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/embedded/umesh)
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


/**
 * A lame implementation of a pseudorandom number generator which can
 * be feed more entropy during runtime. Entropy can be gathered from
 * multiple sources and is port dependent. The least significant bit
 * from a floating ADC input reading can be used to periodically feed
 * more entropy to the prng.
 * 
 * Prng mantains its internal 512bit state which is created by hashing
 * initial seed using SHA-512. When new bits are added to the prng state,
 * new state is computed as SHA-512 of concatenation of previous state
 * and new byte array.
 * 
 * If a random byte array is requested, it is computed as SHA-512 of
 * concatenation of prng's internal state (512 bits) and a 32 bit
 * counter variable incremented on each request.
 */


#include <string.h>
#include <inttypes.h>
#include "sha2.h"
#include "prng.h"


int32_t prng_seed(struct prng_state *state, const uint8_t *data, uint32_t len) {
	sha512(data, len, state->state);
	state->counter = 0;
	
	return PRNG_SEED_OK;
}


int32_t prng_add_entropy(struct prng_state *state, const uint8_t *data, uint32_t len) {
	sha512_ctx ctx;
	
	sha512_init(&ctx);
	sha512_update(&ctx, state->state, 64);
	sha512_update(&ctx, data, len);
	sha512_final(&ctx, state->state);
	
	return PRNG_ADD_ENTROPY_OK;
}


int32_t prng_rand(struct prng_state *state, uint8_t *data, uint32_t len) {
	
	/* longer requests must be made in multiple calls to prng_rand */
	if (len > 64) {
		return PRNG_RAND_TOO_LONG;
	}

	uint8_t res[64];
	sha512_ctx ctx;
	
	sha512_init(&ctx);
	sha512_update(&ctx, state->state, 64);
	sha512_update(&ctx, (uint8_t *)(state->counter), sizeof(state->counter));
	sha512_final(&ctx, res);

	memcpy(data, res, len);

	return PRNG_RAND_OK;
}

