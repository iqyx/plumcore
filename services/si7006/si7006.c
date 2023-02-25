/* SPDX-License-Identifier: BSD-2-Clause
 *
 * SI7006 digital temperature and humidity sensor driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>
#include "si7006.h"

#define MODULE_NAME "si7006"


// self->i2c->transfer(self->i2c->parent, 0x55, &txbuf, 1, (uint8_t *)val, sizeof(uint8_t));

/*********************************************************************************************************************
 * Sensor interface for voltage measurement
 *********************************************************************************************************************/

static sensor_ret_t temperature_value_f(Sensor *self, float *value) {
	Si7006 *si = (Si7006 *)self->parent;

	const uint8_t temp_cmd = 0xe3;
	uint8_t temp[2] = {0};
	si->i2c->transfer(si->i2c->parent, SI7006_ADDR, &temp_cmd, sizeof(temp_cmd), temp, sizeof(temp));
	uint16_t temp16 = temp[0] << 8 | temp[1];
	*value = 175.72f * (float)temp16 / 65536 - 46.85f;

	return SENSOR_RET_OK;
}

static const struct sensor_vmt temperature_vmt = {
	.value_f = temperature_value_f
};

static const struct sensor_info temperature_info = {
	.description = "temperature",
	.unit = "°C"
};


si7006_ret_t si7006_init(Si7006 *self, I2cBus *i2c) {
	memset(self, 0, sizeof(Si7006));
	self->i2c = i2c;

	/* Read the firmware revision. */
	const uint8_t fw_rev_cmd[] = {0x84, 0x88};
	uint8_t fw_rev = 0;
	self->i2c->transfer(self->i2c->parent, SI7006_ADDR, fw_rev_cmd, sizeof(fw_rev_cmd), &fw_rev, sizeof(fw_rev));

	if (fw_rev != 0x20 && fw_rev != 0xff) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("detection failed"));
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("detected, firmware revision 0x%02x"), fw_rev);
	self->temperature.vmt = &temperature_vmt;
	self->temperature.info = &temperature_info;
	self->temperature.parent = self;

	return SI7006_RET_OK;
}


si7006_ret_t si7006_free(Si7006 *self) {
	(void)self;
	return SI7006_RET_OK;
}

