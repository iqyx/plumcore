/**
 * uMeshFw HAL interface
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

#ifndef _HAL_INTERFACE_H_
#define _HAL_INTERFACE_H_

#include <stdint.h>
#include <stdbool.h>
#include "hal_interface_types.h"


struct hal_interface_descriptor {
	/**
	 * This interface name. It should be unique in the HAL.
	 */
	const char *name;

	enum hal_interface_type type;

	uint32_t tags;
	void *iface_ptr;
};



int32_t hal_interface_init(struct hal_interface_descriptor *iface, enum hal_interface_type type);
#define HAL_INTERFACE_INIT_OK 0
#define HAL_INTERFACE_INIT_FAILED -1

int32_t hal_interface_set_name(struct hal_interface_descriptor *iface, const char *name);
#define HAL_INTERFACE_SET_NAME_OK 0
#define HAL_INTERFACE_SET_NAME_FAILED -1

int32_t hal_interface_set_tag(struct hal_interface_descriptor *iface, uint32_t tag);
#define HAL_INTERFACE_SET_TAG_OK 0
#define HAL_INTERFACE_SET_TAG_FAILED -1

int32_t hal_interface_clear_tag(struct hal_interface_descriptor *iface, uint32_t tag);
#define HAL_INTERFACE_CLEAR_TAG_OK 0
#define HAL_INTERFACE_CLEAR_TAG_FAILED -1

bool hal_interface_check_tag(struct hal_interface_descriptor *iface, uint32_t tag);



#endif
