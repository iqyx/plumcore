/**
 * uMeshFw HAL module
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
#include "hal_module.h"
//~ #include "hal_process.h"
#include "hal_interface.h"

int32_t hal_module_descriptor_init(struct hal_module_descriptor *d, const char *name) {
	if (u_assert(d != NULL)) {
		return HAL_MODULE_DESCRIPTOR_INIT_FAILED;
	}

	memset(d, 0, sizeof(struct hal_module_descriptor));
	d->name = name;

	return HAL_MODULE_DESCRIPTOR_INIT_OK;
}


int32_t hal_module_descriptor_set_shm(struct hal_module_descriptor *d, void *addr, size_t len) {
	if (u_assert(d != NULL)) {
		return HAL_MODULE_DESCRIPTOR_SET_SHM_FAILED;
	}

	/* TODO: enforce MPU requirements (memory region must be aligned to at least
	 * its size). */
	d->module_shm = addr;
	d->module_shm_size = len;

	return HAL_MODULE_DESCRIPTOR_SET_SHM_OK;
}
