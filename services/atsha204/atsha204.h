/* SPDX-License-Identifier: BSD-2-Clause
 *
 * ATSHA204 Atmel CryptoAuthentication IC driver
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>

typedef enum {
	ATSHA204_RET_OK = 0,
	ATSHA204_RET_FAILED = -1,
} atsha204_ret_t;


typedef struct {
	I2cBus *i2c;

} Atsha204;


atsha204_ret_t atsha204_init(Atsha204 *self, I2cBus *i2c);
atsha204_ret_t atsha204_free(Atsha204 *self);


