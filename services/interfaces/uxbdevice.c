
/*
 * UXB device interface
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

/* Do not edit this file directly, it is autogenerated. */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "u_assert.h"
#include "interface.h"

#include "uxbdevice.h"

iuxbdevice_ret_t iuxbdevice_init(IUxbDevice *self) {
	if(u_assert(self != NULL)) {
		return IUXBDEVICE_RET_FAILED;
	}

	memset(self, 0, sizeof(IUxbDevice));
	uhal_interface_init(&self->interface);

	return IUXBDEVICE_RET_OK;
}


iuxbdevice_ret_t iuxbdevice_free(IUxbDevice *self) {
	if(u_assert(self != NULL)) {
		return IUXBDEVICE_RET_FAILED;
	}

	return IUXBDEVICE_RET_OK;
}


iuxbdevice_ret_t iuxbdevice_add_slot(IUxbDevice *self, IUxbSlot ** slot) {
	if (self->vmt.add_slot != NULL) {
		return self->vmt.add_slot(self->vmt.context, slot);
	}

	return IUXBDEVICE_RET_FAILED;
}


iuxbdevice_ret_t iuxbdevice_remove_slot(IUxbDevice *self, IUxbSlot * slot) {
	if (self->vmt.remove_slot != NULL) {
		return self->vmt.remove_slot(self->vmt.context, slot);
	}

	return IUXBDEVICE_RET_FAILED;
}


iuxbdevice_ret_t iuxbdevice_set_address(IUxbDevice *self, uint8_t * local_address, uint8_t * remote_address) {
	if (self->vmt.set_address != NULL) {
		return self->vmt.set_address(self->vmt.context, local_address, remote_address);
	}

	return IUXBDEVICE_RET_FAILED;
}


iuxbdevice_ret_t iuxbdevice_get_firmware_version(IUxbDevice *self, char ** buf) {
	if (self->vmt.get_firmware_version != NULL) {
		return self->vmt.get_firmware_version(self->vmt.context, buf);
	}

	return IUXBDEVICE_RET_FAILED;
}


iuxbdevice_ret_t iuxbdevice_read_descriptor(IUxbDevice *self, uint8_t line, char * key, size_t key_size, char * value, size_t value_size) {
	if (self->vmt.read_descriptor != NULL) {
		return self->vmt.read_descriptor(self->vmt.context, line, key, key_size, value, value_size);
	}

	return IUXBDEVICE_RET_FAILED;
}


iuxbdevice_ret_t iuxbdevice_get_hardware_version(IUxbDevice *self, char ** buf) {
	if (self->vmt.get_hardware_version != NULL) {
		return self->vmt.get_hardware_version(self->vmt.context, buf);
	}

	return IUXBDEVICE_RET_FAILED;
}


iuxbdevice_ret_t iuxbdevice_get_name(IUxbDevice *self, char ** buf) {
	if (self->vmt.get_name != NULL) {
		return self->vmt.get_name(self->vmt.context, buf);
	}

	return IUXBDEVICE_RET_FAILED;
}


iuxbdevice_ret_t iuxbdevice_get_id(IUxbDevice *self, uint8_t * id) {
	if (self->vmt.get_id != NULL) {
		return self->vmt.get_id(self->vmt.context, id);
	}

	return IUXBDEVICE_RET_FAILED;
}


