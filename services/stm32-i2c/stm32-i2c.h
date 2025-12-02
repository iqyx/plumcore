/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 I2C driver service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <i2c-bus.h>


typedef enum {
	STM32_I2C_RET_OK = 0,
	STM32_I2C_RET_FAILED = -1,
} stm32_i2c_ret_t;

typedef struct {
	I2cBus bus;
	uint32_t locm3_i2c;
	uint32_t timeout_ms;
	SemaphoreHandle_t bus_lock;
	SemaphoreHandle_t wait_lock;

} Stm32I2c;


stm32_i2c_ret_t stm32_i2c_bus_init(Stm32I2c *self);
stm32_i2c_ret_t stm32_i2c_init(Stm32I2c *self, uint32_t locm3_i2c);
stm32_i2c_ret_t stm32_i2c_free(Stm32I2c *self);
stm32_i2c_ret_t stm32_i2c_irq_handler(Stm32I2c *self);

