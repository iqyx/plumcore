/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 I2C driver service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>
#include <i2c-bus.h>

#if defined(STM32G4)
	#include <stm32g4xx.h>
#elif defined(STM32H7)
	#include <stm32h7xx.h>
#else
	#error "stm32-gpio service is not compatible with this MCU family"
#endif


#include "stm32-i2c.h"

#define MODULE_NAME "stm32-i2c"


stm32_i2c_ret_t stm32_i2c_bus_init(Stm32I2c *self) {
	I2C_TypeDef *base = (I2C_TypeDef *)self->base;
	base->CR1 &= ~I2C_CR1_PE;

	base->CR1 &= ~I2C_CR1_ANFOFF;
	base->CR1 = (base->CR1 & ~(I2C_CR1_DNF_Msk << I2C_CR1_DNF_Pos)) | (0 << I2C_CR1_DNF_Pos);

	int presc = (SystemCoreClock / 4e6) - 1;
	base->TIMINGR =
		(presc << I2C_TIMINGR_PRESC_Pos) |
		(9 << I2C_TIMINGR_SCLL_Pos) |
		(3 << I2C_TIMINGR_SCLH_Pos) |
		(3 << I2C_TIMINGR_SDADEL_Pos) |
		(3 << I2C_TIMINGR_SCLDEL_Pos);
	base->CR1 &= ~I2C_CR1_NOSTRETCH;
	base->CR2 &= ~I2C_CR2_ADD10;
	base->CR1 |= I2C_CR1_PE;

	return STM32_I2C_RET_OK;
}


#define WAIT_FOR_INT(f) \
	base->CR1 |= (f);\
	if (xSemaphoreTake(self->wait_lock, pdMS_TO_TICKS(self->timeout_ms)) != pdTRUE) { \
		return I2C_BUS_RET_FAILED;\
	}\


static i2c_bus_ret_t stm32_i2c_transfer(I2cBus *bus, uint8_t addr, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen) {
	Stm32I2c *self = bus->parent;
	I2C_TypeDef *base = (I2C_TypeDef *)self->base;

	if (xSemaphoreTake(self->bus_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
		xSemaphoreTake(self->wait_lock, 0);
		if (txdata != NULL) {
			/* Implemented according to Master communication initialization (address phase) in RM0440. */
			base->CR2 = (base->CR2 & ~I2C_CR2_SADD_Msk) | ((addr & 0x7F) << 1);
			base->CR2 &= ~I2C_CR2_RD_WRN;
			base->CR2 = (base->CR2 & ~I2C_CR2_NBYTES_Msk) | (txlen << I2C_CR2_NBYTES_Pos);
			base->CR2 &= ~I2C_CR2_AUTOEND;

			base->CR2 |= I2C_CR2_START;

			while (txlen > 0) {
				/* For every byte to be sent, TXIS or NACKF is set, depending on the result
				 * of the previous transfer. Wait for either of those. */
				WAIT_FOR_INT(I2C_CR1_TXIE | I2C_CR1_NACKIE);

				/* Handle NACKF in a specific way, it needs clearing. */
				if (base->ISR & I2C_ISR_NACKF) {
					base->ICR |= I2C_ICR_NACKCF;
					xSemaphoreGive(self->bus_lock);
					return I2C_BUS_RET_NACK;
				}

				base->TXDR = *txdata;

				txlen--;
				txdata++;
			}
			WAIT_FOR_INT(I2C_CR1_TCIE);
			if (rxdata == NULL) {
				/* Only if not repeated start. */
				base->CR2 |= I2C_CR2_STOP;
			}
		}
		if (rxdata != NULL) {
			base->CR2 = (base->CR2 & ~I2C_CR2_SADD_Msk) | ((addr & 0x7F) << 1);
			base->CR2 |= I2C_CR2_RD_WRN;
			base->CR2 = (base->CR2 & ~I2C_CR2_NBYTES_Msk) | (rxlen << I2C_CR2_NBYTES_Pos);
			base->CR2 |= I2C_CR2_START;
			base->CR2 &= ~I2C_CR2_AUTOEND;

			while (rxlen > 0) {
				/* For every byte to be sent, read for RXNE. */
				WAIT_FOR_INT(I2C_CR1_RXIE);
				*rxdata = base->RXDR;

				rxlen--;
				rxdata++;
			}

			WAIT_FOR_INT(I2C_CR1_TCIE);
			base->CR2 |= I2C_CR2_STOP;
		}
	} else {
		/* Try to restart I2C peripheral here. */
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("bus stuck, restarting"));
		stm32_i2c_bus_init(self);
		if (xSemaphoreGetMutexHolder(self->bus_lock) == xTaskGetCurrentTaskHandle()) {
			xSemaphoreGive(self->bus_lock);
		}
		return I2C_BUS_RET_FAILED;
	}

	xSemaphoreGive(self->bus_lock);
	return I2C_BUS_RET_OK;
}


static const struct i2c_bus_vmt stm32_i2c_bus_vmt = {
	.transfer = stm32_i2c_transfer,
};


stm32_i2c_ret_t stm32_i2c_init(Stm32I2c *self, void *base) {
	memset(self, 0, sizeof(Stm32I2c));
	self->base = base;
	self->timeout_ms = 100;

	self->bus_lock = xSemaphoreCreateMutex();
	if (self->bus_lock == NULL) {
		return STM32_I2C_RET_FAILED;
	}

	self->wait_lock = xSemaphoreCreateBinary();
	if (self->wait_lock == NULL) {
		return STM32_I2C_RET_FAILED;
	}

	self->bus.parent = self;
	self->bus.vmt = &stm32_i2c_bus_vmt;

	stm32_i2c_bus_init(self);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("bus initialized"));

	return STM32_I2C_RET_OK;
}


stm32_i2c_ret_t stm32_i2c_free(Stm32I2c *self) {
	vSemaphoreDelete(self->bus_lock);

	return STM32_I2C_RET_OK;
}


stm32_i2c_ret_t stm32_i2c_irq_handler(Stm32I2c *self) {
	I2C_TypeDef *base = (I2C_TypeDef *)self->base;

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	if (base->ISR & I2C_ISR_NACKF) {
		base->CR1 &= ~I2C_CR1_NACKIE;
		xSemaphoreGiveFromISR(self->wait_lock, &xHigherPriorityTaskWoken);
	}
	if (base->ISR & I2C_ISR_TXIS) {
		base->CR1 &= ~I2C_CR1_TXIE;
		xSemaphoreGiveFromISR(self->wait_lock, &xHigherPriorityTaskWoken);
	}
	if (base->ISR & I2C_ISR_TC) {
		base->CR1 &= ~I2C_CR1_TCIE;
		xSemaphoreGiveFromISR(self->wait_lock, &xHigherPriorityTaskWoken);
	}
	if (base->ISR & I2C_ISR_RXNE) {
		base->CR1 &= ~I2C_CR1_RXIE;
		xSemaphoreGiveFromISR(self->wait_lock, &xHigherPriorityTaskWoken);
	}
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

	return STM32_I2C_RET_OK;
}
