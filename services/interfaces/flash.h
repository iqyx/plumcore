/*
 * plumCore flash memory interface
 *
 * Copyright (C) 2018, Marek Koza, qyx@krtko.org
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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "interface.h"

#define IFLASH_SECTOR_SIZES 4

typedef enum {
	IFLASH_RET_OK = 0,
	IFLASH_RET_FAILED,
	IFLASH_RET_UNKNOWN,
	IFLASH_RET_NULL,
	IFLASH_RET_TIMEOUT,
} iflash_ret_t;

struct iflash_info {

	/* Size of the flash memory/partition is defined in Bytes. */
	uint64_t size_bytes;

	/* Flash page is a memory block which can be read or written at once. */
	uint32_t page_size_bytes;

	/* Flash memories have different erase sector sizes. */
	uint32_t sector_size_bytes[IFLASH_SECTOR_SIZES];

	/* JEDEC flash ID. */
	uint32_t id;

	/* String information about the flash manufacturer and part number. */
	char *manufacturer;
	char *part;
};

struct iflash_vmt {
	iflash_ret_t (*get_info)(void *context, struct iflash_info *info);
	iflash_ret_t (*erase)(void *context, const uint64_t addr, uint32_t sector_size_index);
	iflash_ret_t (*write)(void *context, const uint64_t addr, const uint8_t *buf, size_t len);
	iflash_ret_t (*read)(void *context, const uint64_t addr, uint8_t *buf, size_t len);

	void *context;
};

typedef struct {
	Interface interface;

	struct iflash_vmt vmt;
} IFlash;


iflash_ret_t iflash_init(IFlash *self);
iflash_ret_t iflash_free(IFlash *self);
iflash_ret_t iflash_get_info(IFlash *self, struct iflash_info *info);
iflash_ret_t iflash_erase(IFlash *self, const uint64_t addr, uint32_t sector_size_index);
iflash_ret_t iflash_write(IFlash *self, const uint64_t addr, const uint8_t *data, size_t len);
iflash_ret_t iflash_read(IFlash *self, const uint64_t addr, uint8_t *data, size_t len);


