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

#pragma once

#include <stdint.h>
#include "interface.h"


typedef enum {
	INTERFACE_SENSOR_RET_OK = 0,
	INTERFACE_SENSOR_RET_FAILED = -1,
} interface_sensor_ret_t;

typedef struct {
	char *quantity_name; /* eg. temperature */
	char *quantity_symbol; /* eg. t */
	char *unit_name; /* eg. degree Celsius */
	char *unit_symbol; /* eg. °C */

} ISensorInfo;

struct interface_sensor_vmt {
	interface_sensor_ret_t (*info)(void *context, ISensorInfo *info);
	interface_sensor_ret_t (*value)(void *context, float *value);

	void *context;
};


typedef struct {
	Interface interface;

	struct interface_sensor_vmt vmt;

} ISensor;


interface_sensor_ret_t interface_sensor_init(ISensor *self);
interface_sensor_ret_t interface_sensor_info(ISensor *self, ISensorInfo *info);
interface_sensor_ret_t interface_sensor_value(ISensor *self, float *value);



