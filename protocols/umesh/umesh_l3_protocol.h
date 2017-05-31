/**
 * uMeshFw uMesh layer 3 protocol descriptor
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

#ifndef _UMESH_L3_PROTOCOL_H_
#define _UMESH_L3_PROTOCOL_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "umesh_l2_pbuf.h"

/**
 * uMesh Layer 3 protocols with their corresponding numbers.
 */
#define UMESH_L3_PROTOCOL_NBDISCOVERY    0
#define UMESH_L3_PROTOCOL_KEYMGR         1
#define UMESH_L3_PROTOCOL_FILE_TRANSFER  2
#define UMESH_L3_PROTOCOL_STATUS         3

#define UMESH_L3_PROTOCOL_COUNT          16

struct umesh_l3_protocol {
	#define UMESH_L3_PROTOCOL_RECEIVE_OK 0
	#define UMESH_L3_PROTOCOL_RECEIVE_FAILED -1
	int32_t (*receive_handler)(struct umesh_l2_pbuf *pbuf, void *context);

	const char *name;
	void *context;
};


#endif


