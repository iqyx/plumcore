/*
 * plumCore solar charger UXB driver
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
#include "libuxb.h"


typedef enum {
	SOLAR_CHARGER_RET_OK = 0,
	SOLAR_CHARGER_RET_FAILED = -1,
} solar_charger_ret_t;


typedef struct {

	Module module;
	ISensor board_temperature;
	ISensor battery_voltage;
	ISensor battery_current;
	ISensor battery_charge;
	ISensor battery_temperature;

	LibUxbDevice *uxb;
	LibUxbSlot stat_slot;
	uint8_t stat_slot_buffer[64];

	int32_t board_temperature_mc;
	int32_t battery_voltage_mv;
	int32_t battery_current_ma;
	int32_t battery_charge_mah;
	int32_t battery_temperature_mc;

} SolarCharger;


solar_charger_ret_t solar_charger_init(SolarCharger *self, LibUxbDevice *uxb);
solar_charger_ret_t solar_charger_free(SolarCharger *self);

