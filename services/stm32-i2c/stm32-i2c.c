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


/* Recover an I2C bus that is stuck because a slave still drives SDA low, holding an unfinished transaction from
 * before a firmware reset (no power cycle nor reset line). The pins are bit-banged as open-drain outputs: up to
 * nine clock pulses are generated to let the slave flush the pending byte, after which a STOP condition is issued.
 * Meant to be called before stm32_i2c_init() while the bus pins are still plain GPIOs (not yet muxed to the I2C
 * peripheral). */
stm32_i2c_ret_t stm32_i2c_recovery(Gpio *sda, Gpio *scl) {
	if (sda == NULL || scl == NULL) {
		return STM32_I2C_RET_FAILED;
	}

	/* Drive both lines as open-drain outputs with pull-ups; releasing a line lets the pull-up pull it high. */
	sda->vmt->set(sda, true);
	scl->vmt->set(scl, true);
	sda->vmt->set_otype(sda, OTYPE_OD);
	scl->vmt->set_otype(scl, OTYPE_OD);
	sda->vmt->set_pull(sda, PULL_UP);
	scl->vmt->set_pull(scl, PULL_UP);
	sda->vmt->set_mode(sda, MODE_OUTPUT);
	scl->vmt->set_mode(scl, MODE_OUTPUT);
	vTaskDelay(1);

	/* Clock SCL until the slave releases SDA, but no more than nine pulses (eight data bits plus the ACK). */
	for (uint32_t i = 0; i < 9; i++) {
		bool sda_state = false;
		sda->vmt->get(sda, &sda_state);
		if (sda_state) {
			break;
		}

		scl->vmt->set(scl, false);
		vTaskDelay(1);
		scl->vmt->set(scl, true);
		vTaskDelay(1);
	}

	/* Issue a STOP condition: SDA low while SCL is high, then SDA released high. */
	sda->vmt->set(sda, false);
	vTaskDelay(1);
	scl->vmt->set(scl, true);
	vTaskDelay(1);
	sda->vmt->set(sda, true);
	vTaskDelay(1);

	return STM32_I2C_RET_OK;
}


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


/* Arm the requested interrupt(s) and block until the ISR wakes us. Besides the requested flags, the ISR always
 * reports bus error/arbitration-lost/overrun, so check for those on every wakeup and bail out via the common
 * cleanup path. On timeout we also bail out, which guarantees the bus lock is always released. */
#define WAIT_FOR_INT(f) \
	base->CR1 |= (f) | I2C_CR1_ERRIE;\
	if (xSemaphoreTake(self->wait_lock, pdMS_TO_TICKS(self->timeout_ms)) != pdTRUE) { \
		ret = I2C_BUS_RET_FAILED;\
		goto err;\
	}\
	if (base->ISR & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) { \
		ret = I2C_BUS_RET_FAILED;\
		goto err;\
	}\


static i2c_bus_ret_t stm32_i2c_transfer(I2cBus *bus, uint8_t addr, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen) {
	Stm32I2c *self = bus->parent;
	I2C_TypeDef *base = (I2C_TypeDef *)self->base;
	i2c_bus_ret_t ret = I2C_BUS_RET_OK;

	/* NBYTES is an 8-bit field; transfers longer than 255 bytes would need RELOAD mode, which is not implemented. */
	if (txlen > 255 || rxlen > 255) {
		return I2C_BUS_RET_FAILED;
	}

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

				/* Handle NACKF in a specific way, it needs clearing and a STOP to release the bus. */
				if (base->ISR & I2C_ISR_NACKF) {
					ret = I2C_BUS_RET_NACK;
					goto err;
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

err:
	/* Common error exit: release the bus with a STOP, clear all pending status flags and disable the transfer
	 * interrupts so the peripheral is left in a clean idle state for the next transaction. */
	base->CR2 |= I2C_CR2_STOP;
	base->ICR |= I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF | I2C_ICR_STOPCF;
	base->CR1 &= ~(I2C_CR1_TXIE | I2C_CR1_NACKIE | I2C_CR1_RXIE | I2C_CR1_TCIE | I2C_CR1_ERRIE);
	xSemaphoreGive(self->bus_lock);
	return ret;
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
	/* Wait for any ongoing transfer to finish before tearing anything down, otherwise we would delete the
	 * synchronization primitives from under a transfer in progress. The lock is never given back; no transfer
	 * may start once we own it. */
	xSemaphoreTake(self->bus_lock, portMAX_DELAY);

	/* Disable the peripheral before tearing down the synchronization primitives. */
	I2C_TypeDef *base = (I2C_TypeDef *)self->base;
	base->CR1 &= ~I2C_CR1_PE;

	vSemaphoreDelete(self->bus_lock);
	vSemaphoreDelete(self->wait_lock);

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
	/* Bus error, arbitration lost or overrun: wake the waiter so it can abort the transfer. The flags are left
	 * set and cleared on the error cleanup path; only the error interrupt is masked to avoid an ISR storm. */
	if (base->ISR & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
		base->CR1 &= ~I2C_CR1_ERRIE;
		xSemaphoreGiveFromISR(self->wait_lock, &xHigherPriorityTaskWoken);
	}
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

	return STM32_I2C_RET_OK;
}
