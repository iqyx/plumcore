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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "module.h"
#include "interfaces/sensor.h"
#include "libuxb.h"
#include "protocols/uxb/solar_charger_iface.pb.h"
#include "pb_decode.h"
#include "port.h"

#include "solar_charger.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "solar-charger"



static void read_values(SolarCharger *self) {
	uint8_t tmp = 0;
	uint8_t buf[64];
	size_t len = 0;

	if (iuxbslot_send(self->stat_slot, &tmp, 1, false) != IUXBSLOT_RET_OK) {
		return;
	}
	if (iuxbslot_receive(self->stat_slot, buf, 64, &len) != IUXBSLOT_RET_OK) {
		return;
	}

	pb_istream_t stream;
        stream = pb_istream_from_buffer(buf, len);
	SolarChargerResponse msg;

	if (pb_decode(&stream, SolarChargerResponse_fields, &msg)) {
		self->battery_voltage_mv = msg.battery_voltage_mv;
		self->battery_current_ma = msg.battery_current_ma;
		self->battery_charge_mah = msg.battery_charge_mah;
		self->board_temperature_mc = msg.board_temperature_mc;
		self->battery_temperature_mc = msg.battery_temperature_mc;
	} else {
		self->battery_voltage_mv = 0;
		self->battery_current_ma = 0;
		self->battery_charge_mah = 0;
		self->board_temperature_mc = 0;
		self->battery_temperature_mc = 0;
	}
}


static interface_sensor_ret_t board_temperature_mc_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	info->quantity_name = "Temperature";
	info->quantity_symbol = "t";
	info->unit_name = "Degree Celsius";
	info->unit_symbol = "m°C";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t board_temperature_mc_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	SolarCharger *self = (SolarCharger *)context;
	read_values(self);
	*value = self->board_temperature_mc;

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t battery_voltage_mv_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	info->quantity_name = "Voltage";
	info->quantity_symbol = "U";
	info->unit_name = "milliVolt";
	info->unit_symbol = "mV";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t battery_voltage_mv_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	SolarCharger *self = (SolarCharger *)context;
	read_values(self);
	*value = self->battery_voltage_mv;

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t battery_current_ma_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	info->quantity_name = "Current";
	info->quantity_symbol = "I";
	info->unit_name = "microAmps";
	info->unit_symbol = "uA";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t battery_current_ma_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	SolarCharger *self = (SolarCharger *)context;
	read_values(self);
	*value = self->battery_current_ma;

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t battery_charge_mah_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	info->quantity_name = "Charge";
	info->quantity_symbol = "q";
	info->unit_name = "milliAmp/hour";
	info->unit_symbol = "mAh";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t battery_charge_mah_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	SolarCharger *self = (SolarCharger *)context;
	read_values(self);
	*value = self->battery_charge_mah;

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t battery_temperature_mc_info(void *context, ISensorInfo *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	info->quantity_name = "Temperature";
	info->quantity_symbol = "t";
	info->unit_name = "Degree Celsius";
	info->unit_symbol = "m°C";

	return INTERFACE_SENSOR_RET_OK;
}


static interface_sensor_ret_t battery_temperature_mc_value(void *context, float *value) {
	if (u_assert(context != NULL) ||
	    u_assert(value != NULL)) {
		return INTERFACE_SENSOR_RET_FAILED;
	}

	SolarCharger *self = (SolarCharger *)context;
	read_values(self);
	*value = self->battery_temperature_mc;

	return INTERFACE_SENSOR_RET_OK;
}


solar_charger_ret_t solar_charger_init(SolarCharger *self,  IUxbSlot *slot) {
	if (u_assert(self != NULL)) {
		return SOLAR_CHARGER_RET_FAILED;
	}

	memset(self, 0, sizeof(SolarCharger));
	uhal_module_init(&self->module);

	iuxbslot_set_slot_buffer(slot, self->stat_slot_buffer, 64);
	self->stat_slot = slot;

	interface_sensor_init(&(self->board_temperature));
	self->board_temperature.vmt.info = board_temperature_mc_info;
	self->board_temperature.vmt.value = board_temperature_mc_value;
	self->board_temperature.vmt.context = (void *)self;
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_SENSOR,
		&self->board_temperature.interface,
		"charger_board_temp"
	);

	interface_sensor_init(&(self->battery_voltage));
	self->battery_voltage.vmt.info = battery_voltage_mv_info;
	self->battery_voltage.vmt.value = battery_voltage_mv_value;
	self->battery_voltage.vmt.context = (void *)self;
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_SENSOR,
		&self->battery_voltage.interface,
		"charger_bat_voltage"
	);

	interface_sensor_init(&(self->battery_current));
	self->battery_current.vmt.info = battery_current_ma_info;
	self->battery_current.vmt.value = battery_current_ma_value;
	self->battery_current.vmt.context = (void *)self;
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_SENSOR,
		&self->battery_current.interface,
		"charger_bat_current"
	);

	interface_sensor_init(&(self->battery_charge));
	self->battery_charge.vmt.info = battery_charge_mah_info;
	self->battery_charge.vmt.value = battery_charge_mah_value;
	self->battery_charge.vmt.context = (void *)self;
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_SENSOR,
		&self->battery_charge.interface,
		"charger_bat_charge"
	);

	interface_sensor_init(&(self->battery_temperature));
	self->battery_temperature.vmt.info = battery_temperature_mc_info;
	self->battery_temperature.vmt.value = battery_temperature_mc_value;
	self->battery_temperature.vmt.context = (void *)self;
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_SENSOR,
		&self->battery_temperature.interface,
		"charger_bat_temp"
	);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("UXB solar charger driver initialized"));
	return SOLAR_CHARGER_RET_OK;
}

solar_charger_ret_t solar_charger_free(SolarCharger *self) {
	if (u_assert(self != NULL)) {
		return SOLAR_CHARGER_RET_FAILED;
	}

	return SOLAR_CHARGER_RET_OK;
}


