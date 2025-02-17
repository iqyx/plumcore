/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LDC1312, LDC1314 Multi-Channel 12-Bit Inductance to Digital Converter driver
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>

#include <interfaces/sensor.h>
#include <interfaces/i2c-bus.h>


typedef enum  {
	LDC1X1X_RET_OK = 0,
	LDC1X1X_RET_FAILED,
} ldc1x1x_ret_t;


typedef struct ldc1x1x {
	I2cBus *i2c;
	uint8_t addr;

	uint8_t *preselect_cmd;
	size_t preselect_cmd_len;
	uint8_t preselect_cmd_addr;

	Sensor out[4];

	SemaphoreHandle_t select_lock;
} Ldc1x1x;


ldc1x1x_ret_t ldc1x1x_init(Ldc1x1x *self, I2cBus *i2c, uint8_t addr);
ldc1x1x_ret_t ldc1x1x_enable(Ldc1x1x *self);
ldc1x1x_ret_t ldc1x1x_set_preselect_cmd(Ldc1x1x *self, uint8_t *cmd, size_t len, uint8_t addr);

