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

#ifndef _INTERFACE_PROFILING_H_
#define _INTERFACE_PROFILING_H_

#include <stdint.h>
#include "hal_interface.h"

struct interface_profiling_vmt {

	int32_t (*event)(void *context, uint8_t module, uint8_t message, int16_t param);
	void *context;
};

struct interface_profiling {
	struct hal_interface_descriptor descriptor;
	struct interface_profiling_vmt vmt;
};


int32_t interface_profiling_init(struct interface_profiling *pro);
#define INTERFACE_PROFILING_INIT_OK 0
#define INTERFACE_PROFILING_INIT_FAILED -1

int32_t interface_profiling_event(struct interface_profiling *pro, uint8_t module, uint8_t message, int16_t param);
#define INTERFACE_PROFILING_EVENT_OK 0
#define INTERFACE_PROFILING_EVENT_FAILED -1


#endif

