/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Driver for EEPROM memories connected over the I2C bus
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/flash.h>


typedef enum {
	I2C_EEPROM_RET_OK = 0,
	I2C_EEPROM_RET_FAILED,
	I2C_EEPROM_RET_NULL,
} i2c_eeprom_ret_t;


typedef struct {
	bool initialized;
	size_t eeprom_size;
	size_t page_size;
	uint8_t addr;

	I2cBus *i2c;
	Flash flash;
} I2cEeprom;


i2c_eeprom_ret_t i2c_eeprom_init(I2cEeprom *self, I2cBus *i2c, uint8_t addr, size_t eeprom_size, size_t page_size);
i2c_eeprom_ret_t i2c_eeprom_free(I2cEeprom *self);
