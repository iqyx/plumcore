/**
 * uMeshFw abstract time profiler interface
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
#include "interface_profiling.h"


int32_t interface_profiling_init(struct interface_profiling *pro) {
	if (u_assert(pro != NULL)) {
		return INTERFACE_PROFILING_INIT_FAILED;
	}

	memset(pro, 0, sizeof(struct interface_profiling));
	hal_interface_init(&(pro->descriptor), HAL_INTERFACE_TYPE_PROFILING);

	return INTERFACE_PROFILING_INIT_OK;
}


int32_t interface_profiling_event(struct interface_profiling *pro, uint8_t module, uint8_t message, int16_t param) {
	if (u_assert(pro != NULL)) {
		return INTERFACE_PROFILING_EVENT_FAILED;
	}
	if (pro->vmt.event != NULL) {
		return pro->vmt.event(pro->vmt.context, module, message, param);
	}
	return INTERFACE_PROFILING_EVENT_OK;
}

/** @todo enable live output on the selected stream */
/** @todo dump profiling buffer to a stream */
