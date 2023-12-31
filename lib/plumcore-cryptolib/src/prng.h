/**
 * pseudorandom numbr generator
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

#ifndef _PRNG_H_
#define _PRNG_H_


struct prng_state {
	uint8_t state[64];
	uint32_t counter;
};





int32_t prng_seed(struct prng_state *state, const uint8_t *data, uint32_t len);
#define PRNG_SEED_OK 0
#define PRNG_SEED_FAILED -1

int32_t prng_add_entropy(struct prng_state *state, const uint8_t *data, uint32_t len);
#define PRNG_ADD_ENTROPY_OK 0
#define PRNG_ADD_ENTROPY_FAILED -1
	
int32_t prng_rand(struct prng_state *state, uint8_t *data, uint32_t len);
#define PRNG_RAND_OK 0
#define PRNG_RAND_TOO_LONG -1
#define PRNG_RAND_LOW_ENTROPY -2

	




#endif

