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
#elif defined(STM32U5)
	#include <stm32u5xx.h>
#else
	#error "stm32-i2c service is not compatible with this MCU family"
#endif


#include "stm32-i2c.h"

#define MODULE_NAME "stm32-i2c"

/* Ports mux the I2C kernel clock to HSI16, a fixed 16 MHz source on all supported families. */
#define I2C_KERNEL_CLOCK_HZ 16000000UL


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

	/* The I2C peripheral is clocked from HSI16 (16 MHz) on every port, so its timing is independent of the core
	 * clock: the SYSCLK may be raised well above the 4-bit PRESC field's reach without affecting the bus. The
	 * prescaler normalizes the 16 MHz kernel clock to a 4 MHz internal timer (250 ns per tick), so
	 * SCLL/SCLH/SDADEL/SCLDEL below are expressed in 250 ns units. At this granularity the fast speeds are coarse,
	 * but that is the best resolution available at this kernel clock. */
	uint32_t presc = (I2C_KERNEL_CLOCK_HZ / 4e6) - 1;
	uint32_t scll = 0;
	uint32_t sclh = 0;
	uint32_t sdadel = 0;
	uint32_t scldel = 0;
	switch (self->speed_hz) {
		case 1000000:
			/* Fast-mode plus, ~1 MHz. */
			scll = 1;
			sclh = 1;
			sdadel = 0;
			scldel = 1;
			break;
		case 100000:
			/* Standard mode, ~100 kHz. */
			scll = 0x13;
			sclh = 0x0f;
			sdadel = 2;
			scldel = 4;
			break;
		case 10000:
			/* Super-slow, ~10 kHz. SCLL/SCLH are 8-bit, so the 100 us period is split into two ~50 us
			 * halves of 200 ticks (0xc7 + 1) each. Useful for long or weakly pulled-up buses. */
			scll = 0xc7;
			sclh = 0xc7;
			sdadel = 4;
			scldel = 8;
			break;
		case 400000:
		default:
			/* Fast mode, ~400 kHz (also used as a safe fallback for unsupported speeds). */
			scll = 5;
			sclh = 3;
			sdadel = 1;
			scldel = 2;
			break;
	}
	base->TIMINGR =
		(presc << I2C_TIMINGR_PRESC_Pos) |
		(scll << I2C_TIMINGR_SCLL_Pos) |
		(sclh << I2C_TIMINGR_SCLH_Pos) |
		(sdadel << I2C_TIMINGR_SDADEL_Pos) |
		(scldel << I2C_TIMINGR_SCLDEL_Pos);
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
		/* Clear a STOPF possibly left set by a previously aborted transfer, so it cannot make the ISR
		 * report a spurious stop while this transfer waits for TXIS/TC. */
		base->ICR = I2C_ICR_STOPCF;
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
				/* No repeated start follows: issue a STOP and wait for it to actually complete before
				 * releasing the bus, so the next transfer never sets START while this STOP is still in
				 * progress. STOPF is cleared here as nothing else clears it on the success path. */
				base->CR2 |= I2C_CR2_STOP;
				WAIT_FOR_INT(I2C_CR1_STOPIE);
				base->ICR = I2C_ICR_STOPCF;
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
			WAIT_FOR_INT(I2C_CR1_STOPIE);
			base->ICR = I2C_ICR_STOPCF;
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
	/* Common error exit. Disable the transfer interrupts first so the ISR cannot fire while we tear the
	 * transaction down, then recover the peripheral depending on the failure. Either way the bus lock is
	 * always released so the bus never stays stuck. */
	base->CR1 &= ~(I2C_CR1_TXIE | I2C_CR1_NACKIE | I2C_CR1_RXIE | I2C_CR1_TCIE | I2C_CR1_STOPIE | I2C_CR1_ERRIE);
	if (ret == I2C_BUS_RET_NACK) {
		/* A NACK is a normal slave response: the state machine is still healthy, so just release the bus
		 * with a STOP and clear the pending flags. */
		base->CR2 |= I2C_CR2_STOP;
		base->ICR |= I2C_ICR_NACKCF | I2C_ICR_STOPCF;
	} else {
		/* A timeout or a bus/arbitration/overrun error means an expected TXIS/RXNE/TC event never arrived
		 * and the peripheral state machine is wedged. A STOP alone does not recover it, so fully
		 * re-initialize the peripheral (PE off/on), which resets the state machine and clears all flags. */
		stm32_i2c_bus_init(self);
	}
	xSemaphoreGive(self->bus_lock);
	return ret;
}


static const struct i2c_bus_vmt stm32_i2c_bus_vmt = {
	.transfer = stm32_i2c_transfer,
};


/* Probe a single 7-bit address by starting an address-only write (zero data bytes) with a hardware STOP right
 * after the address phase (AUTOEND). No data byte is written, so a present slave is never disturbed. The addressed
 * slave either ACKs the address (NACKF stays clear) or NACKs it; either way the peripheral generates the STOP and
 * raises STOPF, on which the ISR wakes us. */
static i2c_bus_ret_t stm32_i2c_probe(Stm32I2c *self, uint8_t addr) {
	I2C_TypeDef *base = (I2C_TypeDef *)self->base;
	i2c_bus_ret_t ret = I2C_BUS_RET_OK;

	if (xSemaphoreTake(self->bus_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
		return I2C_BUS_RET_FAILED;
	}
	xSemaphoreTake(self->wait_lock, 0);
	base->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;

	base->CR2 = (((uint32_t)addr & 0x7F) << 1) | I2C_CR2_AUTOEND | I2C_CR2_START;
	base->CR1 |= I2C_CR1_STOPIE | I2C_CR1_ERRIE;
	if (xSemaphoreTake(self->wait_lock, pdMS_TO_TICKS(self->timeout_ms)) != pdTRUE) {
		/* The expected STOP never completed; the peripheral state machine is wedged, re-initialize it. */
		base->CR1 &= ~(I2C_CR1_STOPIE | I2C_CR1_ERRIE);
		stm32_i2c_bus_init(self);
		xSemaphoreGive(self->bus_lock);
		return I2C_BUS_RET_FAILED;
	}
	if (base->ISR & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) {
		ret = I2C_BUS_RET_FAILED;
		stm32_i2c_bus_init(self);
	} else if (base->ISR & I2C_ISR_NACKF) {
		ret = I2C_BUS_RET_NACK;
	}
	base->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;

	xSemaphoreGive(self->bus_lock);
	return ret;
}


/* Scan the whole 7-bit address range and report every address that ACKs its address phase via u_log. Only the
 * general-purpose range 0x08..0x77 is probed; the reserved addresses outside it are skipped. */
stm32_i2c_ret_t stm32_i2c_scan(Stm32I2c *self) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("scanning I2C bus..."));
	for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
		if (stm32_i2c_probe(self, addr) == I2C_BUS_RET_OK) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("device found at address 0x%02x"), addr);
		}
	}
	return STM32_I2C_RET_OK;
}


stm32_i2c_ret_t stm32_i2c_init(Stm32I2c *self, void *base) {
	memset(self, 0, sizeof(Stm32I2c));
	self->base = base;
	self->timeout_ms = 100;
	self->speed_hz = 400000;

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


/* Change the bus speed at runtime. The new setting is stored and the peripheral is re-initialized so it takes
 * effect. The bus lock is held for the whole operation so no transfer can run while the peripheral is disabled. */
stm32_i2c_ret_t stm32_i2c_set_speed(Stm32I2c *self, uint32_t speed_hz) {
	xSemaphoreTake(self->bus_lock, portMAX_DELAY);
	self->speed_hz = speed_hz;
	stm32_i2c_bus_init(self);
	xSemaphoreGive(self->bus_lock);

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
	if (base->ISR & I2C_ISR_STOPF) {
		base->CR1 &= ~I2C_CR1_STOPIE;
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
