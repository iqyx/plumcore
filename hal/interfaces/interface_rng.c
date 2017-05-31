/**
 * uMeshFw HAL random number generator interface
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
#include "interface_rng.h"


int32_t interface_rng_init(struct interface_rng *rng) {
	if (u_assert(rng != NULL)) {
		return INTERFACE_RNG_INIT_FAILED;
	}

	memset(rng, 0, sizeof(struct interface_rng));
	hal_interface_init(&(rng->descriptor), HAL_INTERFACE_TYPE_RNG);

	return INTERFACE_RNG_INIT_OK;
}


int32_t interface_rng_read(struct interface_rng *rng, uint8_t *buf, size_t len) {
	if (u_assert(rng != NULL)) {
		return INTERFACE_RNG_READ_FAILED;
	}
	if (rng->vmt.read != NULL) {
		return rng->vmt.read(rng->vmt.context, buf, len);
	}
	return INTERFACE_RNG_READ_FAILED;
}

