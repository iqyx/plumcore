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


iflash_ret_t iflash_init(IFlash *self) {
	if (u_assert(self != NULL)) {
		return IFLASH_RET_FAILED;
	}

	memset(self, 0, sizeof(IFlash));
	uhal_interface_init(&self->interface);

	return IFLASH_RET_OK;
}


iflash_ret_t iflash_free(IFlash *self) {
	if (u_assert(self != NULL)) {
		return IFLASH_RET_FAILED;
	}

	/* Nothing to free yet. */

	return IFLASH_RET_OK;
}


iflash_ret_t iflash_get_info(IFlash *self, struct iflash_info *info) {
	if (u_assert(self != NULL) ||
	    u_assert(info != NULL)) {
		return IFLASH_RET_FAILED;
	}

	if (self->vmt.get_info != NULL) {
		return self->vmt.get_info(self->vmt.context, info);
	}

	return IFLASH_RET_FAILED;
}


iflash_ret_t iflash_erase(IFlash *self, const uint64_t addr, uint32_t sector_size_index) {
	if (u_assert(self != NULL)) {
		return IFLASH_RET_FAILED;
	}

	if (self->vmt.erase != NULL) {
		return self->vmt.erase(self->vmt.context, addr, sector_size_index);
	}

	return IFLASH_RET_FAILED;
}


iflash_ret_t iflash_write(IFlash *self, const uint64_t addr, const uint8_t *data, size_t len) {
	if (u_assert(self != NULL) ||
	    u_assert(data != NULL) ||
	    u_assert(len > 0)) {
		return IFLASH_RET_FAILED;
	}

	if (self->vmt.write != NULL) {
		return self->vmt.write(self->vmt.context, addr, data, len);
	}

	return IFLASH_RET_FAILED;
}


iflash_ret_t iflash_read(IFlash *self, const uint64_t addr, uint8_t *data, size_t len) {
	if (u_assert(self != NULL) ||
	    u_assert(data != NULL) ||
	    u_assert(len > 0)) {
		return IFLASH_RET_FAILED;
	}

	if (self->vmt.read != NULL) {
		return self->vmt.read(self->vmt.context, addr, data, len);
	}

	return IFLASH_RET_FAILED;
}

