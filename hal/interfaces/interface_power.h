/*
 * uMeshFw HAL power device interface
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

#pragma once

#include <stdint.h>
#include "hal_interface.h"


struct interface_power_vmt {
	int32_t (*measure)(void *context);
	int32_t (*voltage)(void *context);
	int32_t (*current)(void *context);
	int32_t (*capacity)(void *context);
	void *context;
};

struct interface_power {
	struct hal_interface_descriptor descriptor;

	struct interface_power_vmt vmt;

};


int32_t interface_power_init(struct interface_power *self);
#define INTERFACE_POWER_INIT_OK 0
#define INTERFACE_POWER_INIT_FAILED -1

int32_t interface_power_measure(struct interface_power *self);
#define INTERFACE_POWER_MEASURE_OK 0
#define INTERFACE_POWER_MEASURE_FAILED -1

int32_t interface_power_voltage(struct interface_power *self);
#define INTERFACE_POWER_VOLTAGE_OK 0
#define INTERFACE_POWER_VOLTAGE_FAILED -1

int32_t interface_power_current(struct interface_power *self);
#define INTERFACE_POWER_CURRENT_OK 0
#define INTERFACE_POWER_CURRENT_FAILED -1

int32_t interface_power_capacity(struct interface_power *self);
#define INTERFACE_POWER_CAPACITY_OK 0
#define INTERFACE_POWER_CAPACITY_FAILED -1


