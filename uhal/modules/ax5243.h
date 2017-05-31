/*
 * ON Semiconductor AX5243 Advanced High Performance ASK and FSK Narrow-band
 * Transceiver for 27-1050 MHz Range driver module
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/embedded/umesh)
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

/* uHal generic includes and interfaces */
#include "module.h"

/* Old HAL interfaces. */
#include "interface_netdev.h"
#include "interface_spidev.h"


typedef enum {
	AX5243_RET_OK = 0,
	AX5243_RET_FAILED = -1,
} ax5243_ret_t;


typedef struct {
	Module module;
	uint32_t fxtal;

	struct interface_netdev iface;
	struct interface_spidev *spidev;

} Ax5243;


ax5243_ret_t ax5243_init(Ax5243 *self, struct interface_spidev *spidev, uint32_t xtal_hz);
ax5243_ret_t ax5243_free(Ax5243 *self);

