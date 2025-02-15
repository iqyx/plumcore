/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VCNL4035 ALS/proximity sensor
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>

#include <interfaces/sensor.h>
#include <interfaces/i2c-bus.h>

#include "vcnl4035.h"

#define MODULE_NAME "vcnl4035"


static vcnl4035_ret_t als_write(Vcnl4035 *self, uint8_t addr, uint16_t reg) {
	uint8_t tx[3] = {
		addr,
		reg & 0xff,
		reg >> 8
	};
	self->i2c->transfer(self->i2c->parent, self->addr, tx, sizeof(tx), NULL, 0);

	return VCNL4035_RET_OK;
}


static vcnl4035_ret_t als_read(Vcnl4035 *self, uint8_t addr, uint16_t *reg) {
	uint8_t rx[2] = {0};
	self->i2c->transfer(self->i2c->parent, self->addr, &addr, 1, rx, sizeof(rx));
	if (reg != NULL) {
		*reg = rx[1] << 8 | rx[0];
		return VCNL4035_RET_OK;
	}

	return VCNL4035_RET_FAILED;
}


static sensor_ret_t vcnl4035_sensor_value_f(Sensor *sensor, float *value) {
	Vcnl4035 *self = sensor->parent;

	uint16_t val = 0;
	if (sensor == &self->ps1) {
		als_read(self, 0x08, &val);
	} else if (sensor == &self->ps2) {
		als_read(self, 0x09, &val);
	} else if (sensor == &self->ps3) {
		als_read(self, 0x0a, &val);
	} else if (sensor == &self->als) {
		als_read(self, 0x0b, &val);
	} else {
		return SENSOR_RET_FAILED;
	}
	if (value != NULL) {
		*value = val;
		return SENSOR_RET_OK;
	}

	return SENSOR_RET_FAILED;
}


static const struct sensor_vmt vcnl4035_sensor_vmt = {
	.value_f = vcnl4035_sensor_value_f,
};


static const struct sensor_info vcnl4035_sensor_info = {
	.description = "Proximity sensor output",
	.unit = "-"
};

static const struct sensor_info vcnl4035_sensor_info2 = {
	.description = "Ambient light intensity",
	.unit = "-"
};


vcnl4035_ret_t vcnl_4035_init(Vcnl4035 *self, I2cBus *i2c, uint8_t addr) {
	memset(self, 0, sizeof(Vcnl4035));
	self->i2c = i2c;
	self->addr = addr;

	/* Check if ALS is accessible. */
	uint16_t id = 0;
	als_read(self, 0x0e, &id);
	als_write(self, 0x00, 0x0000);
	als_write(self, 0x03, 0x3cce);
	/* 200 mA driving current. */
	als_write(self, 0x04, 0x0700);
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("ALS id = 0x%04x"), id);

	self->ps1.vmt = &vcnl4035_sensor_vmt;
	self->ps1.info = &vcnl4035_sensor_info;
	self->ps1.parent = self;

	self->ps2.vmt = &vcnl4035_sensor_vmt;
	self->ps2.info = &vcnl4035_sensor_info;
	self->ps2.parent = self;

	self->ps3.vmt = &vcnl4035_sensor_vmt;
	self->ps3.info = &vcnl4035_sensor_info;
	self->ps3.parent = self;

	self->als.vmt = &vcnl4035_sensor_vmt;
	self->als.info = &vcnl4035_sensor_info2;
	self->als.parent = self;

	return VCNL4035_RET_OK;
}

