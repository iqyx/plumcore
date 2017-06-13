/*
 * plumCore flash memory interface
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
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


typedef enum {
	FLASH_RET_OK = 0,
	FLASH_RET_FAILED = -1,
	FLASH_RET_UNKNOWN = -2,
} flash_ret_t;

struct flash_info {
	uint64_t capacity;
	uint32_t page_size_bytes;
	uint32_t sector_size_pages;
	uint32_t block_size_sectors;
	uint32_t id;
	char *manufacturer;
	char *part;
};

struct flash_vmt {
	flash_ret_t (*get_id)(void *context, uint32_t *id);
	flash_ret_t (*get_info)(void *context, struct flash_info *info);
	flash_ret_t (*chip_erase)(void *context);
	flash_ret_t (*block_erase)(void *context, const uint64_t addr);
	flash_ret_t (*sector_erase)(void *context, const uint64_t addr);
	flash_ret_t (*page_write)(void *context, const uint64_t addr, const uint8_t *data, const uint32_t len);
	flash_ret_t (*page_read)(void *context, const uint64_t addr, uint8_t *data, const uint32_t len);

	void *context;
};

typedef struct {
	Interface interface;

	struct flash_vmt vmt;

} IFlash;


flash_ret_t flash_init(IFlash *self);
flash_ret_t flash_free(IFlash *self);
flash_ret_t flash_get_id(IFlash *self, uint32_t *id);
flash_ret_t flash_get_info(IFlash *self, struct flash_info *info);
flash_ret_t flash_chip_erase(IFlash *self);
flash_ret_t flash_block_erase(IFlash *self, const uint64_t addr);
flash_ret_t flash_sector_erase(IFlash *self, const uint64_t addr);
flash_ret_t flash_page_write(IFlash *self, const uint64_t addr, const uint8_t *data, const uint32_t len);
flash_ret_t flash_page_read(IFlash *self, const uint64_t addr, uint8_t *data, const uint32_t len);


