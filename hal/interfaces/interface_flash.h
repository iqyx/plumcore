/**
 * uMeshFw flash memory interface
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
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

#ifndef _INTERFACE_FLASH_H_
#define _INTERFACE_FLASH_H_

#include <stdint.h>
#include <stdbool.h>
#include "hal_interface.h"


struct interface_flash_info {
	uint32_t capacity;
	uint32_t page_size;
	uint32_t sector_size;
	uint32_t block_size;
	char *manufacturer;
	char *part;
	uint32_t id;
};

struct interface_flash_vmt {
	int32_t (*get_id)(void *context, uint32_t *id);
	int32_t (*get_info)(void *context, struct interface_flash_info *info);
	int32_t (*chip_erase)(void *context);
	int32_t (*block_erase)(void *context, const uint32_t addr);
	int32_t (*sector_erase)(void *context, const uint32_t addr);
	int32_t (*page_write)(void *context, const uint32_t addr, const uint8_t *data, const uint32_t len);
	int32_t (*page_read)(void *context, const uint32_t addr, uint8_t *data, const uint32_t len);

	void *context;
};

struct interface_flash {
	struct hal_interface_descriptor descriptor;
	struct interface_flash_vmt vmt;
	bool init_ok;
};


int32_t interface_flash_init(struct interface_flash *flash);
#define INTERFACE_FLASH_INIT_OK 0
#define INTERFACE_FLASH_INIT_FAILED -1

int32_t interface_flash_free(struct interface_flash *flash);
#define INTERFACE_FLASH_FREE_OK 0
#define INTERFACE_FLASH_FREE_FAILED -1

int32_t interface_flash_get_id(struct interface_flash *flash, uint32_t *id);
#define INTERFACE_FLASH_GET_ID_OK 0
#define INTERFACE_FLASH_GET_ID_FAILED -1

int32_t interface_flash_get_info(struct interface_flash *flash, struct interface_flash_info *info);
#define INTERFACE_FLASH_GET_INFO_OK 0
#define INTERFACE_FLASH_GET_INFO_FAILED -1
#define INTERFACE_FLASH_GET_INFO_UNKNOWN -2

int32_t interface_flash_chip_erase(struct interface_flash *flash);
#define INTERFACE_FLASH_CHIP_ERASE_OK 0
#define INTERFACE_FLASH_CHIP_ERASE_FAILED -1

int32_t interface_flash_block_erase(struct interface_flash *flash, const uint32_t addr);
#define INTERFACE_FLASH_BLOCK_ERASE_OK 0
#define INTERFACE_FLASH_BLOCK_ERASE_FAILED -1

int32_t interface_flash_sector_erase(struct interface_flash *flash, const uint32_t addr);
#define INTERFACE_FLASH_SECTOR_ERASE_OK 0
#define INTERFACE_FLASH_SECTOR_ERASE_FAILED -1

int32_t interface_flash_page_write(struct interface_flash *flash, const uint32_t addr, const uint8_t *data, const uint32_t len);
#define INTERFACE_FLASH_PAGE_WRITE_OK 0
#define INTERFACE_FLASH_PAGE_WRITE_FAILED -1

int32_t interface_flash_page_read(struct interface_flash *flash, const uint32_t addr, uint8_t *data, const uint32_t len);
#define INTERFACE_FLASH_PAGE_READ_OK 0
#define INTERFACE_FLASH_PAGE_READ_FAILED -1





#endif
