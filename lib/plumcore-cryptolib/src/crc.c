/**
 * CRC calculations
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


#include <inttypes.h>
#include "crc.h"

uint16_t crc16_byte(uint16_t crc, uint8_t b) {
	for (int i = 0; i < 8; i++) {
		if (((crc & 0x8000) >> 8) ^ (b & 0x80)) {
			crc = (crc << 1) ^ CRC16_POLY;
		} else {
			crc = (crc << 1);
		}
		
		b <<= 1;
	}
	
	return crc;
}

uint16_t crc16(const uint8_t *data, uint32_t len) {
	uint16_t crc = CRC16_INIT;
	
	for (uint32_t i = 0; i < len; i++) {
		crc = crc16_byte(crc, data[i]);
	}

	return crc;
}

