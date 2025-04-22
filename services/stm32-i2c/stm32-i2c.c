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
#include <libopencm3/stm32/rcc.h>

#include "stm32-i2c.h"
#include <i2c-bus.h>

#define MODULE_NAME "stm32-i2c"


stm32_i2c_ret_t stm32_i2c_bus_init(Stm32I2c *self) {
	i2c_peripheral_disable(self->locm3_i2c);
	//~ i2c_reset(self->locm3_i2c);
	i2c_enable_analog_filter(self->locm3_i2c);
	i2c_set_digital_filter(self->locm3_i2c, 0);
	i2c_set_speed(self->locm3_i2c, i2c_speed_sm_100k, rcc_apb1_frequency / 1e6);
	i2c_enable_stretching(self->locm3_i2c);
	i2c_set_7bit_addr_mode(self->locm3_i2c);
	i2c_peripheral_enable(self->locm3_i2c);

	return STM32_I2C_RET_OK;
}


#define WAIT_FOR_INT(f) \
	I2C_CR1(self->locm3_i2c) |= (f);\
	if (xSemaphoreTake(self->wait_lock, pdMS_TO_TICKS(self->timeout_ms)) != pdTRUE) { \
		return I2C_BUS_RET_FAILED;\
	}\


static i2c_bus_ret_t stm32_i2c_transfer(Stm32I2c *self, uint8_t addr, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen) {
	if (xSemaphoreTake(self->bus_lock, portMAX_DELAY) == pdTRUE) {
		xSemaphoreTake(self->wait_lock, 0);
		if (txdata != NULL) {
			/* Implemented according to Master communication initialization (address phase) in RM0440. */
			I2C_CR2(self->locm3_i2c) = (I2C_CR2(self->locm3_i2c) & ~I2C_CR2_SADD_7BIT_MASK) | ((addr & 0x7F) << I2C_CR2_SADD_7BIT_SHIFT);
			I2C_CR2(self->locm3_i2c) &= ~I2C_CR2_RD_WRN;
			I2C_CR2(self->locm3_i2c) = (I2C_CR2(self->locm3_i2c) & ~I2C_CR2_NBYTES_MASK) | (txlen << I2C_CR2_NBYTES_SHIFT);
			I2C_CR2(self->locm3_i2c) &= ~I2C_CR2_AUTOEND;

			I2C_CR2(self->locm3_i2c) |= I2C_CR2_START;

			while (txlen > 0) {
				/* For every byte to be sent, TXIS or NACKF is set, depending on the result
				 * of the previous transfer. Wait for either of those. */
				WAIT_FOR_INT(I2C_CR1_TXIE | I2C_CR1_NACKIE);

				/* Handle NACKF in a specific way, it needs clearing. */
				if (I2C_ISR(self->locm3_i2c) & I2C_ISR_NACKF) {
					I2C_ICR(self->locm3_i2c) |= I2C_ICR_NACKCF;
					xSemaphoreGive(self->bus_lock);
					return I2C_BUS_RET_NACK;
				}

				I2C_TXDR(self->locm3_i2c) = *txdata;

				txlen--;
				txdata++;
			}
			WAIT_FOR_INT(I2C_CR1_TCIE);
			if (rxdata == NULL) {
				/* Only if not repeated start. */
				I2C_CR2(self->locm3_i2c) |= I2C_CR2_STOP;
			}
		}
		if (rxdata != NULL) {
			I2C_CR2(self->locm3_i2c) = (I2C_CR2(self->locm3_i2c) & ~I2C_CR2_SADD_7BIT_MASK) | ((addr & 0x7F) << I2C_CR2_SADD_7BIT_SHIFT);
			I2C_CR2(self->locm3_i2c) |= I2C_CR2_RD_WRN;
			I2C_CR2(self->locm3_i2c) = (I2C_CR2(self->locm3_i2c) & ~I2C_CR2_NBYTES_MASK) | (rxlen << I2C_CR2_NBYTES_SHIFT);
			I2C_CR2(self->locm3_i2c) |= I2C_CR2_START;
			I2C_CR2(self->locm3_i2c) &= ~I2C_CR2_AUTOEND;

			while (rxlen > 0) {
				/* For every byte to be sent, read for RXNE. */
				WAIT_FOR_INT(I2C_CR1_RXIE);
				*rxdata = I2C_RXDR(self->locm3_i2c);

				rxlen--;
				rxdata++;
			}

			WAIT_FOR_INT(I2C_CR1_TCIE);
			I2C_CR2(self->locm3_i2c) |= I2C_CR2_STOP;
		}
	} else {
		return I2C_BUS_RET_FAILED;
	}

	xSemaphoreGive(self->bus_lock);
	return I2C_BUS_RET_OK;
}


stm32_i2c_ret_t stm32_i2c_init(Stm32I2c *self, uint32_t locm3_i2c) {
	memset(self, 0, sizeof(Stm32I2c));
	self->locm3_i2c = locm3_i2c;
	self->timeout_ms = 100;

	self->bus_lock = xSemaphoreCreateMutex();
	if (self->bus_lock == NULL) {
		return STM32_I2C_RET_FAILED;
	}

	self->wait_lock = xSemaphoreCreateBinary();
	if (self->wait_lock == NULL) {
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


stm32_i2c_ret_t stm32_i2c_irq_handler(Stm32I2c *self) {
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	if (I2C_ISR(self->locm3_i2c) & I2C_ISR_NACKF) {
		I2C_CR1(self->locm3_i2c) &= ~I2C_CR1_NACKIE;
		xSemaphoreGiveFromISR(self->wait_lock, &xHigherPriorityTaskWoken);
	}
	if (I2C_ISR(self->locm3_i2c) & I2C_ISR_TXIS) {
		I2C_CR1(self->locm3_i2c) &= ~I2C_CR1_TXIE;
		xSemaphoreGiveFromISR(self->wait_lock, &xHigherPriorityTaskWoken);
	}
	if (I2C_ISR(self->locm3_i2c) & I2C_ISR_TC) {
		I2C_CR1(self->locm3_i2c) &= ~I2C_CR1_TCIE;
		xSemaphoreGiveFromISR(self->wait_lock, &xHigherPriorityTaskWoken);
	}
	if (I2C_ISR(self->locm3_i2c) & I2C_ISR_RXNE) {
		I2C_CR1(self->locm3_i2c) &= ~I2C_CR1_RXIE;
		xSemaphoreGiveFromISR(self->wait_lock, &xHigherPriorityTaskWoken);
	}
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

	return STM32_I2C_RET_OK;
}
