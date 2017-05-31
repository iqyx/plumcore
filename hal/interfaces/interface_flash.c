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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "u_assert.h"
#include "hal_interface.h"
#include "interface_flash.h"


int32_t interface_flash_init(struct interface_flash *flash) {
	if (u_assert(flash != NULL)) {
		return INTERFACE_FLASH_INIT_FAILED;
	}

	memset(flash, 0, sizeof(struct interface_flash));
	hal_interface_init(&(flash->descriptor), HAL_INTERFACE_TYPE_FLASH);
	flash->init_ok = true;

	return INTERFACE_FLASH_INIT_OK;
}


int32_t interface_flash_free(struct interface_flash *flash) {
	if (u_assert(flash != NULL && flash->init_ok == true)) {
		return INTERFACE_FLASH_FREE_FAILED;
	}

	flash->init_ok = false;

	return INTERFACE_FLASH_FREE_OK;
}


int32_t interface_flash_get_id(struct interface_flash *flash, uint32_t *id) {
	if (u_assert(flash != NULL && flash->init_ok == true)) {
		return INTERFACE_FLASH_GET_ID_FAILED;
	}
	if (flash->vmt.get_id != NULL) {
		return flash->vmt.get_id(flash->vmt.context, id);
	}
	return INTERFACE_FLASH_GET_ID_FAILED;
}


int32_t interface_flash_get_info(struct interface_flash *flash, struct interface_flash_info *info) {
	if (u_assert(flash != NULL &&  flash->init_ok == true)) {
		return INTERFACE_FLASH_GET_INFO_FAILED;
	}
	if (flash->vmt.get_info != NULL) {
		return flash->vmt.get_info(flash->vmt.context, info);
	}
	return INTERFACE_FLASH_GET_INFO_FAILED;
}


int32_t interface_flash_chip_erase(struct interface_flash *flash) {
	if (u_assert(flash != NULL &&  flash->init_ok == true)) {
		return INTERFACE_FLASH_CHIP_ERASE_FAILED;
	}
	if (flash->vmt.chip_erase != NULL) {
		return flash->vmt.chip_erase(flash->vmt.context);
	}
	return INTERFACE_FLASH_CHIP_ERASE_FAILED;
}


int32_t interface_flash_block_erase(struct interface_flash *flash, const uint32_t addr) {
	if (u_assert(flash != NULL &&  flash->init_ok == true)) {
		return INTERFACE_FLASH_BLOCK_ERASE_FAILED;
	}
	if (flash->vmt.block_erase != NULL) {
		return flash->vmt.block_erase(flash->vmt.context, addr);
	}
	return INTERFACE_FLASH_BLOCK_ERASE_FAILED;
}


int32_t interface_flash_sector_erase(struct interface_flash *flash, const uint32_t addr) {
	if (u_assert(flash != NULL &&  flash->init_ok == true)) {
		return INTERFACE_FLASH_SECTOR_ERASE_FAILED;
	}
	if (flash->vmt.sector_erase != NULL) {
		return flash->vmt.sector_erase(flash->vmt.context, addr);
	}
	return INTERFACE_FLASH_SECTOR_ERASE_FAILED;
}


int32_t interface_flash_page_write(struct interface_flash *flash, const uint32_t addr, const uint8_t *data, const uint32_t len) {
	if (u_assert(flash != NULL &&  flash->init_ok == true)) {
		return INTERFACE_FLASH_PAGE_WRITE_FAILED;
	}
	if (flash->vmt.page_write != NULL) {
		return flash->vmt.page_write(flash->vmt.context, addr, data, len);
	}
	return INTERFACE_FLASH_PAGE_WRITE_FAILED;
}


int32_t interface_flash_page_read(struct interface_flash *flash, const uint32_t addr, uint8_t *data, const uint32_t len) {
	if (u_assert(flash != NULL &&  flash->init_ok == true)) {
		return INTERFACE_FLASH_PAGE_READ_FAILED;
	}
	if (flash->vmt.page_read != NULL) {
		return flash->vmt.page_read(flash->vmt.context, addr, data, len);
	}
	return INTERFACE_FLASH_PAGE_READ_FAILED;
}
