/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LDC1312, LDC1314 Multi-Channel 12-Bit Inductance to Digital Converter driver
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

#include "ldc1x1x.h"

#define MODULE_NAME "ldc1x1x"


static ldc1x1x_ret_t ldc1x1x_select(Ldc1x1x *self) {
	if (self->preselect_cmd != NULL) {
		self->i2c->transfer(self->i2c->parent, self->preselect_cmd_addr, self->preselect_cmd, self->preselect_cmd_len, NULL, 0);
		vTaskDelay(1);
	}
	return LDC1X1X_RET_OK;
}


static ldc1x1x_ret_t ldc_write(Ldc1x1x *self, uint8_t addr, uint16_t reg) {
	uint8_t tx[3] = {
		addr,
		reg >> 8,
		reg & 0xff
	};
	self->i2c->transfer(self->i2c->parent, self->addr, tx, sizeof(tx), NULL, 0);

	return LDC1X1X_RET_OK;
}


static ldc1x1x_ret_t ldc_read(Ldc1x1x *self, uint8_t addr, uint16_t *reg) {
	uint8_t rx[2] = {0};
	self->i2c->transfer(self->i2c->parent, self->addr, &addr, 1, rx, sizeof(rx));
	if (reg != NULL) {
		*reg = rx[0] << 8 | rx[1];
		return LDC1X1X_RET_OK;
	}

	return LDC1X1X_RET_FAILED;
}


static sensor_ret_t ldc1x1x_sensor_value_f(Sensor *sensor, float *value) {
	Ldc1x1x *self = sensor->parent;

	ldc1x1x_select(self);

	if (value != NULL) {
		for (size_t i = 0; i < 4; i++) {
			if (sensor == &(self->out[i])) {
				uint16_t val = 0;
				ldc_read(self, 0x00 + i * 2, &val);
				*value = val;
				return SENSOR_RET_OK;
			}
		}
	}

	return SENSOR_RET_FAILED;
}


static const struct sensor_vmt ldc1x1x_sensor_vmt = {
	.value_f = ldc1x1x_sensor_value_f,
};


static const struct sensor_info ldc1x1x_sensor_info = {
	.description = "LDC input value",
	.unit = "-"
};


ldc1x1x_ret_t ldc1x1x_init(Ldc1x1x *self, I2cBus *i2c, uint8_t addr) {
	memset(self, 0, sizeof(Ldc1x1x));
	self->i2c = i2c;
	self->addr = addr;

	for (size_t i = 0; i < 4; i++) {
		self->out[i].vmt = &ldc1x1x_sensor_vmt;
		self->out[i].info = &ldc1x1x_sensor_info;
		self->out[i].parent = self;
	}

	return LDC1X1X_RET_OK;
}


ldc1x1x_ret_t ldc1x1x_enable(Ldc1x1x *self) {
	ldc1x1x_select(self);

	for (size_t i = 0; i < 4; i++) {
		ldc_write(self, 0x14 + i, 0x0004);
	}

	ldc_write(self, 0x1b, 0xc20d);
	ldc_write(self, 0x1a, 0x0001);

	return LDC1X1X_RET_OK;
}


ldc1x1x_ret_t ldc1x1x_set_preselect_cmd(Ldc1x1x *self, uint8_t *cmd, size_t len, uint8_t addr) {
	self->preselect_cmd = cmd;
	self->preselect_cmd_len = len;
	self->preselect_cmd_addr = addr;

	return LDC1X1X_RET_OK;
}

