/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VCNL4035 ALS/proximity sensor
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>

#include <interfaces/sensor.h>
#include <interfaces/i2c-bus.h>


typedef enum  {
	VCNL4035_RET_OK = 0,
	VCNL4035_RET_FAILED,
} vcnl4035_ret_t;


typedef struct vcnl4035 {
	I2cBus *i2c;
	uint8_t addr;

	Sensor ps1;
	Sensor ps2;
	Sensor ps3;
	Sensor als;
} Vcnl4035;


vcnl4035_ret_t vcnl_4035_init(Vcnl4035 *self, I2cBus *i2c, uint8_t addr);

