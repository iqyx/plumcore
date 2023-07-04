/* SPDX-License-Identifier: BSD-2-Clause
 *
 * ATSHA204 Atmel CryptoAuthentication IC driver
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/sensor.h>
#include "atsha204.h"

#define MODULE_NAME "atsha204"


atsha204_ret_t atsha204_init(Atsha204 *self, I2cBus *i2c) {
	memset(self, 0, sizeof(Atsha204));
	self->i2c = i2c;

	return ATSHA204_RET_OK;
}


atsha204_ret_t atsha204_free(Atsha204 *self) {
	(void)self;
	return ATSHA204_RET_OK;
}

