/* SPDX-License-Identifier: BSD-2-Clause
 *
 * SI7006 digital temperature and humidity sensor driver
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <i2c-bus.h>

#include <main.h>

#include <interfaces/sensor.h>

#define SI7006_ADDR 0x40

typedef enum {
	SI7006_RET_OK = 0,
	SI7006_RET_FAILED = -1,
} si7006_ret_t;


typedef struct {
	I2cBus *i2c;

	Sensor temperature;
} Si7006;


si7006_ret_t si7006_init(Si7006 *self, I2cBus *i2c);
si7006_ret_t si7006_free(Si7006 *self);


