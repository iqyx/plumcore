/*
 * plumCore I2C sensor test
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

#include "module.h"
#include "interfaces/sensor.h"


typedef enum {
	I2C_SENSORS_RET_OK = 0,
	I2C_SENSORS_RET_FAILED = -1,
} i2c_sensors_ret_t;


typedef struct {

	Module module;
	ISensor si7021_temp;
	ISensor si7021_rh;
	ISensor lum;

	uint32_t i2c;

} I2cSensors;


i2c_sensors_ret_t i2c_sensors_init(I2cSensors *self, uint32_t i2c);
i2c_sensors_ret_t i2c_sensors_free(I2cSensors *self);
