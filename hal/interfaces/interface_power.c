/*
 * uMeshFw HAL power device interface
 *
 * Copyright (C) 2016, Marek Koza, qyx@krtko.org
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
#include "interface_power.h"
#include "hal_interface.h"


int32_t interface_power_init(struct interface_power *self) {
	if (u_assert(self != NULL)) {
		return INTERFACE_POWER_INIT_FAILED;
	}

	memset(self, 0, sizeof(struct interface_power));
	hal_interface_init(&(self->descriptor), HAL_INTERFACE_TYPE_POWER);

	return INTERFACE_POWER_INIT_OK;
}


int32_t interface_power_measure(struct interface_power *self) {
	if (u_assert(self != NULL)) {
		return INTERFACE_POWER_MEASURE_FAILED;
	}
	if (self->vmt.measure != NULL) {
		return self->vmt.measure(self->vmt.context);
	}
	return INTERFACE_POWER_MEASURE_OK;
}


int32_t interface_power_voltage(struct interface_power *self) {
	if (u_assert(self != NULL)) {
		return INTERFACE_POWER_VOLTAGE_FAILED;
	}
	if (self->vmt.voltage != NULL) {
		return self->vmt.voltage(self->vmt.context);
	}
	return INTERFACE_POWER_VOLTAGE_OK;
}


int32_t interface_power_current(struct interface_power *self) {
	if (u_assert(self != NULL)) {
		return INTERFACE_POWER_CURRENT_FAILED;
	}
	if (self->vmt.current != NULL) {
		return self->vmt.current(self->vmt.context);
	}
	return INTERFACE_POWER_CURRENT_OK;
}


int32_t interface_power_capacity(struct interface_power *self) {
	if (u_assert(self != NULL)) {
		return INTERFACE_POWER_CAPACITY_FAILED;
	}
	if (self->vmt.capacity != NULL) {
		return self->vmt.capacity(self->vmt.context);
	}
	return INTERFACE_POWER_CAPACITY_OK;
}

