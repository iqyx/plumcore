/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * I2C bus interface
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>


typedef enum {
	I2C_BUS_RET_OK = 0,
	I2C_BUS_RET_FAILED,
	I2C_BUS_RET_NACK,
} i2c_bus_ret_t;

typedef struct i2c_bus I2cBus;

struct i2c_bus_vmt {
	i2c_bus_ret_t (*transfer)(I2cBus *self, uint8_t addr, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen);
};

typedef struct i2c_bus {
	const struct i2c_bus_vmt *vmt;
	void *parent;
} I2cBus;




