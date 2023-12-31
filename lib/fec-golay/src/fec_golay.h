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

#ifndef _FEC_GOLAY_H_
#define _FEC_GOLAY_H_


/* Two different polynomials are available */
//~ #define GOLAY_POLY 0xc75
#define FEC_GOLAY_POLY 0xae3


uint32_t fec_golay_encode(uint32_t codeword);
uint32_t fec_golay_syndrome(uint32_t codeword);
uint32_t fec_golay_weight_codeword(uint32_t codeword);
uint32_t fec_golay_correct(uint32_t codeword);
void fec_golay_table_fill(void);



#endif
