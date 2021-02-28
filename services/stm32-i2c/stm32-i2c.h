/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 I2C driver service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <i2c-bus.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"


typedef enum {
	STM32_I2C_RET_OK = 0,
	STM32_I2C_RET_FAILED = -1,
} stm32_i2c_ret_t;

typedef struct {
	I2cBus bus;
	uint32_t locm3_i2c;
	uint32_t timeout;
	SemaphoreHandle_t bus_lock;
	
} Stm32I2c;


stm32_i2c_ret_t stm32_i2c_bus_init(Stm32I2c *self);
stm32_i2c_ret_t stm32_i2c_init(Stm32I2c *self, uint32_t locm3_i2c);
stm32_i2c_ret_t stm32_i2c_free(Stm32I2c *self);

i2c_bus_ret_t stm32_i2c_transfer(Stm32I2c *self, uint8_t addr, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen);

