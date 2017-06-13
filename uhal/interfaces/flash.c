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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "u_assert.h"
#include "interface.h"

#include "flash.h"


flash_ret_t flash_init(IFlash *self) {
	if (u_assert(self != NULL)) {
		return FLASH_RET_FAILED;
	}

	memset(self, 0, sizeof(IFlash));
	uhal_interface_init(&self->interface);

	return FLASH_RET_OK;
}


flash_ret_t flash_free(IFlash *self) {
	if (u_assert(self != NULL)) {
		return FLASH_RET_FAILED;
	}

	/* Nothing to free yet. */

	return FLASH_RET_OK;
}


flash_ret_t flash_get_id(IFlash *self, uint32_t *id) {
	if (u_assert(self != NULL) ||
	    u_assert(id != NULL)) {
		return FLASH_RET_FAILED;
	}

	if (self->vmt.get_id != NULL) {
		return self->vmt.get_id(self->vmt.context, id);
	}

	return FLASH_RET_FAILED;
}


flash_ret_t flash_get_info(IFlash *self, struct flash_info *info) {
	if (u_assert(self != NULL) ||
	    u_assert(info != NULL)) {
		return FLASH_RET_FAILED;
	}

	if (self->vmt.get_info != NULL) {
		return self->vmt.get_info(self->vmt.context, info);
	}

	return FLASH_RET_FAILED;
}


flash_ret_t flash_chip_erase(IFlash *self) {
	if (u_assert(self != NULL)) {
		return FLASH_RET_FAILED;
	}

	if (self->vmt.chip_erase != NULL) {
		return self->vmt.chip_erase(self->vmt.context);
	}

	return FLASH_RET_FAILED;
}


flash_ret_t flash_block_erase(IFlash *self, const uint64_t addr) {
	if (u_assert(self != NULL)) {
		return FLASH_RET_FAILED;
	}

	if (self->vmt.block_erase != NULL) {
		return self->vmt.block_erase(self->vmt.context, addr);
	}

	return FLASH_RET_FAILED;
}


flash_ret_t flash_sector_erase(IFlash *self, const uint64_t addr) {
	if (u_assert(self != NULL)) {
		return FLASH_RET_FAILED;
	}

	if (self->vmt.sector_erase != NULL) {
		return self->vmt.sector_erase(self->vmt.context, addr);
	}

	return FLASH_RET_FAILED;
}


flash_ret_t flash_page_write(IFlash *self, const uint64_t addr, const uint8_t *data, const uint32_t len) {
	if (u_assert(self != NULL) ||
	    u_assert(data != NULL) ||
	    u_assert(len > 0)) {
		return FLASH_RET_FAILED;
	}

	if (self->vmt.page_write != NULL) {
		return self->vmt.page_write(self->vmt.context, addr, data, len);
	}

	return FLASH_RET_FAILED;
}


flash_ret_t flash_page_read(IFlash *self, const uint64_t addr, uint8_t *data, const uint32_t len) {
	if (u_assert(self != NULL) ||
	    u_assert(data != NULL) ||
	    u_assert(len > 0)) {
		return FLASH_RET_FAILED;
	}

	if (self->vmt.page_read != NULL) {
		return self->vmt.page_read(self->vmt.context, addr, data, len);
	}

	return FLASH_RET_FAILED;
}

