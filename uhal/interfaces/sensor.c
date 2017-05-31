/*
 * uMeshFw HAL Sensor interface
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
#include "adc.h"

#include "sensor.h"

interface_sensor_ret_t interface_sensor_init(ISensor *self) {
	if (u_assert(self != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	memset(self, 0, sizeof(ISensor));
	uhal_interface_init(&self->interface);

	return INTERFACE_SENSOR_RET_OK;
}


interface_sensor_ret_t interface_sensor_info(ISensor *self, ISensorInfo *info) {
	if (u_assert(self != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	if (self->vmt.info != NULL) {
		return self->vmt.info(self->vmt.context, info);
	}
	return INTERFACE_SENSOR_RET_FAILED;
}


interface_sensor_ret_t interface_sensor_value(ISensor *self, float *value) {
	if (u_assert(self != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	if (self->vmt.value != NULL) {
		return self->vmt.value(self->vmt.context, value);
	}
	return INTERFACE_SENSOR_RET_FAILED;
}

