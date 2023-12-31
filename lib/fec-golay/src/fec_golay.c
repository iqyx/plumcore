/**
 * Golay(24, 12) code error correction
 *
 * Implementation based on document available at
 * http://www.aqdi.com/golay.htm
 * (C) 2003 Hank Wallace
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


#include <stdint.h>
#include "fec_golay.h"


/**
 * A table for decoding golay codewords. Index is determined by codeword
 * syndrome (11bit left part) and the value says how to construct 23bit
 * correcting pattern. Saving whole 23bit pattern in a 32bit unsigned
 * integer is avoided due to memory constraints (we can save 50% of space)
 *
 * uint16_t [x i i i i i j j j j j k k k k k]
 *
 * x -       unused bit
 * i, j, k - indexes of '1' bits in the pattern from the right (LSb). Maximum
 *           of three '1's can be specified (this is the maximum amount of
 *           correctable bits)
 */
static uint16_t fec_golay_table[2048];


/**
 * Computes 11 checkbits for specified 12 information bits (codeword parameter).
 * Checkbits are saved before information bits and the result is returned.
 *
 * uint32_t [xxxxxxxxxccccccccccciiiiiiiiiiii]
 *
 * x - unused bits
 * c - check bits
 * i - right justified information bits passed as an codeword argument
 */
uint32_t fec_golay_encode(uint32_t codeword) {
	/* codeword can contain only 12 bits of information, crop the rest */
	codeword &= 0xfff;

	uint32_t c = codeword;

	for (int i = 0; i < 12; i++) {
		if (c & 1) {
			c ^= FEC_GOLAY_POLY;
		}
		c >>= 1;
	}

	/* returns original codeword prefixed by 11 checkbits */
	return codeword | (c << 12);
}


/**
 * Compute golay syndrome of received 23bit codeword. Syndrome is 11bit
 * value saved in a 32bit unsigned integer right justified.
 */
uint32_t fec_golay_syndrome(uint32_t codeword) {
	/* codeword contains 23bits, crop the rest */
	codeword &= 0x7fffff;

	for (int i = 0; i < 12; i++) {
		if (codeword & 1) {
			codeword ^= FEC_GOLAY_POLY;
		}
		codeword >>= 1;
	}

	return codeword;
}


/**
 * Computes weight of a 23bit codeword (number of '1's)
 */
uint32_t fec_golay_weight_codeword(uint32_t codeword) {
	const uint8_t weights[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};

	return weights[codeword & 0xf] + weights[(codeword >> 4) & 0xf] +  weights[(codeword >> 8) & 0xf];
}


/**
 * Performs error correction on a received 23bit codeword. A syndrome is computed
 * first. If it equals to zero, codeword was received uncorrupted and no
 * corrections are needed (function returns). Otherwise, decoding table lookup
 * is performed using the syndrome value and error correction pattern is constructed.
 * Codeword is xor'ed with this pattern to correct errors.
 *
 * If there is more than 3 bit errors in a single codeword, the function returns
 * bad result (expected behaviour).
 */
uint32_t fec_golay_correct(uint32_t codeword) {
	uint32_t syn = fec_golay_syndrome(codeword);

	if (syn == 0) {
		/* if syndrome == 0, there's no need to correct anything,
		 * return original codeword in this situation */
		return codeword;
	}

	uint32_t pattern =
		(1 << ((fec_golay_table[syn] >> 10) & 0x1f)) |
		(1 << ((fec_golay_table[syn] >> 5 ) & 0x1f)) |
		(1 << ((fec_golay_table[syn]      ) & 0x1f));

	return codeword ^ pattern;
}


/**
 * Prepare decoding table (shared global)
 */
void fec_golay_table_fill(void) {
	/* Try all combinations of bit errors. Some combinations
	 * are iterated multiple times. As this is done only once during
	 * initialization, things are kept simple. */
	for (int i = 0; i < 23; i++) {
		for (int j = 0; j < 23; j++) {
			for (int k = 0; k < 23; k++) {
				uint32_t pattern = (1 << i) | (1 << j) | (1 << k);
				uint32_t syn = fec_golay_syndrome(pattern);

				//~ if (golay_weight_codeword(pattern) < table[syn]) {
					fec_golay_table[syn] = (i << 10) | (j << 5) | (k);
				//~ }
			}
		}
	}
	/* First table item doesn't need to be initialized (zero syndrome
	 * means no need for error correction). Left here for completeness. */
	fec_golay_table[0] = 0;
}

