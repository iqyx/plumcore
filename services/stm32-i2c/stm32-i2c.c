/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 I2C driver service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <libopencm3/stm32/i2c.h>

#include "interface_spidev.h"
#include "stm32-i2c.h"
#include <i2c-bus.h>

#define MODULE_NAME "stm32-i2c"


stm32_i2c_ret_t stm32_i2c_bus_init(Stm32I2c *self) {
	i2c_peripheral_disable(self->locm3_i2c);
	i2c_reset(self->locm3_i2c);
	i2c_enable_analog_filter(self->locm3_i2c);
	i2c_set_digital_filter(self->locm3_i2c, 0);
	i2c_set_speed(self->locm3_i2c, i2c_speed_sm_100k, 16);
	i2c_enable_stretching(self->locm3_i2c);
	i2c_set_7bit_addr_mode(self->locm3_i2c);
	i2c_peripheral_enable(self->locm3_i2c);
	return STM32_I2C_RET_OK;
}


i2c_bus_ret_t stm32_i2c_transfer(Stm32I2c *self, uint8_t addr, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen) {
	if (xSemaphoreTake(self->bus_lock, portMAX_DELAY) == pdTRUE) {
		i2c_transfer7(self->locm3_i2c, addr, (uint8_t *)txdata, txlen, rxdata, rxlen);
		xSemaphoreGive(self->bus_lock);
	} else {
		return I2C_BUS_RET_FAILED;
	}
	return I2C_BUS_RET_OK;
}


stm32_i2c_ret_t stm32_i2c_init(Stm32I2c *self, uint32_t locm3_i2c) {
	memset(self, 0, sizeof(Stm32I2c));
	self->locm3_i2c = locm3_i2c;
	self->timeout = 1000;

	self->bus_lock = xSemaphoreCreateMutex();
	if (self->bus_lock == NULL) {
		return STM32_I2C_RET_FAILED;
	}

	i2c_bus_init(&self->bus);
	self->bus.parent = self;
	self->bus.transfer = (typeof(self->bus.transfer))stm32_i2c_transfer;

	stm32_i2c_bus_init(self);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("bus initialized"));
		
	return STM32_I2C_RET_OK;
}


stm32_i2c_ret_t stm32_i2c_free(Stm32I2c *self) {
	vSemaphoreDelete(self->bus_lock);
	return STM32_I2C_RET_OK;
}
