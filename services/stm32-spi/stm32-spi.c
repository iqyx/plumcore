/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 SPI driver service
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>

#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/gpio.h>

#include <interfaces/spi.h>
#include "stm32-spi.h"

#define MODULE_NAME "stm32-spi"


/***************************************************************************************************
 * SpiBus interface implementation
 ***************************************************************************************************/


static spi_ret_t stm32_spibus_send(SpiBus *spibus, const uint8_t *txbuf, size_t txlen) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;

	for (size_t i = 0; i < txlen; i++) {
		#if defined(STM32L4) || defined(STM32G4)
			spi_send8(self->locm3_spi, txbuf[i]);
			spi_read8(self->locm3_spi);
		#else
			spi_xfer(self->locm3_spi, txbuf[i]);
		#endif
	}

	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_receive(SpiBus *spibus, uint8_t *rxbuf, size_t rxlen) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;

	for (size_t i = 0; i < rxlen; i++) {
		#if defined(STM32L4) || defined(STM32G4)
			spi_send8(self->locm3_spi, 0x00);
			rxbuf[i] = spi_read8(self->locm3_spi);
		#else
			rxbuf[i] = spi_xfer(self->locm3_spi, 0x00);
		#endif
	}

	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_exchange(SpiBus *spibus, const uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;

	for (size_t i = 0; i < len; i++) {
		#if defined(STM32L4) || defined(STM32G4)
			spi_send8(self->locm3_spi, txbuf[i]);
			rxbuf[i] = spi_read8(self->locm3_spi);
		#else
			rxbuf[i] = spi_xfer(self->locm3_spi, txbuf[i]);
		#endif
	}

	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_lock(SpiBus *spibus) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;
	if (xSemaphoreTake(self->bus_lock, portMAX_DELAY) != pdTRUE) {
		return SPI_RET_NOT_AVAILABLE;
	}
	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_unlock(SpiBus *spibus) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;
	xSemaphoreGive(self->bus_lock);
	return SPI_RET_OK;
}


static const uint8_t baudrate_prescalers[] = {
	SPI_CR1_BR_FPCLK_DIV_2,
	SPI_CR1_BR_FPCLK_DIV_4,
	SPI_CR1_BR_FPCLK_DIV_8,
	SPI_CR1_BR_FPCLK_DIV_16,
	SPI_CR1_BR_FPCLK_DIV_32,
	SPI_CR1_BR_FPCLK_DIV_64,
	SPI_CR1_BR_FPCLK_DIV_128,
	SPI_CR1_BR_FPCLK_DIV_256,
};

static spi_ret_t stm32_spibus_set_sck_freq(SpiBus *spibus, uint32_t freq_hz) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;

	/** @todo get the correct APB frequency properly */
	extern uint32_t rcc_apb1_frequency;

	spi_disable(self->locm3_spi);
	uint32_t i = 0;
	uint8_t prescaler = 2;
	while (rcc_apb1_frequency / prescaler > freq_hz && i < sizeof(baudrate_prescalers) - 1) {
		i++;
		prescaler *= 2;
	}
	spi_set_baudrate_prescaler(self->locm3_spi, baudrate_prescalers[i]);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SCK freq requested = %lu kHz, prescaler = %u, real = %lu kHz"), freq_hz / 1000, prescaler, rcc_apb1_frequency / prescaler / 1000);
	spi_enable(self->locm3_spi);

	return SPI_RET_OK;
}


static spi_ret_t stm32_spibus_set_mode(SpiBus *spibus, uint8_t cpol, uint8_t cpha) {
	Stm32SpiBus *self = (Stm32SpiBus *)spibus->parent;

	spi_disable(self->locm3_spi);
	if (cpol == 0) {
		spi_set_clock_polarity_0(self->locm3_spi);
	} else {
		spi_set_clock_polarity_1(self->locm3_spi);
	}
	if (cpha == 0) {
		spi_set_clock_phase_0(self->locm3_spi);
	} else {
		spi_set_clock_phase_1(self->locm3_spi);
	}
	spi_enable(self->locm3_spi);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("mode set to CPOL=%u,CPHA=%u"), cpol, cpha);

	return SPI_RET_OK;
}


static const struct spibus_vmt stm32_spibus_vmt = {
	.send = stm32_spibus_send,
	.receive = stm32_spibus_receive,
	.exchange = stm32_spibus_exchange,
	.lock = stm32_spibus_lock,
	.unlock = stm32_spibus_unlock,
	.set_sck_freq = stm32_spibus_set_sck_freq,
	.set_mode = stm32_spibus_set_mode,
};


stm32_spi_ret_t stm32_spibus_init(Stm32SpiBus *self, uint32_t locm3_spi) {
	memset(self, 0, sizeof(Stm32SpiBus));
	self->locm3_spi = locm3_spi;

	self->bus_lock = xSemaphoreCreateMutex();
	if (self->bus_lock == NULL) {
		return STM32_SPI_RET_FAILED;
	}

	self->bus.parent = self;
	self->bus.vmt = &stm32_spibus_vmt;

	spi_disable(self->locm3_spi);
	spi_set_master_mode(self->locm3_spi);
	spi_set_baudrate_prescaler(self->locm3_spi, SPI_CR1_BR_FPCLK_DIV_32);
	spi_set_clock_polarity_0(self->locm3_spi);
	spi_set_clock_phase_0(self->locm3_spi);
	spi_set_full_duplex_mode(self->locm3_spi);
	spi_set_unidirectional_mode(self->locm3_spi);
	spi_enable_software_slave_management(self->locm3_spi);
	spi_send_msb_first(self->locm3_spi);
	spi_set_nss_high(self->locm3_spi);
	#if defined(STM32L4) || defined(STM32G4)
		spi_fifo_reception_threshold_8bit(self->locm3_spi);
		spi_set_data_size(self->locm3_spi, SPI_CR2_DS_8BIT);
	#endif
	spi_enable(self->locm3_spi);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SPI bus %p initialised"), self->locm3_spi);

	return STM32_SPI_RET_OK;
}


stm32_spi_ret_t stm32_spibus_free(Stm32SpiBus *self) {
	vSemaphoreDelete(self->bus_lock);
	return STM32_SPI_RET_OK;
}


/***************************************************************************************************
 * SpiBus interface implementation
 ***************************************************************************************************/

static spi_ret_t stm32_spidev_send(SpiDev *spidev, const uint8_t *txbuf, size_t txlen) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;
	if (!self->selected) {
		return SPI_RET_FAILED;
	}
	return self->bus->vmt->send(self->bus, txbuf, txlen);
}


static spi_ret_t stm32_spidev_receive(SpiDev *spidev, uint8_t *rxbuf, size_t rxlen) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;
	if (!self->selected) {
		return SPI_RET_FAILED;
	}

	return self->bus->vmt->receive(self->bus, rxbuf, rxlen);
}


static spi_ret_t stm32_spidev_exchange(SpiDev *spidev, const uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;
	if (!self->selected) {
		return SPI_RET_FAILED;
	}

	return self->bus->vmt->exchange(self->bus, txbuf, rxbuf, len);
}


static spi_ret_t stm32_spidev_select(SpiDev *spidev) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;

	if (self->bus->vmt->lock(self->bus) != SPI_RET_OK) {
		return SPI_RET_FAILED;
	}
	gpio_clear(self->locm3_cs_port, self->locm3_cs_pin);
	self->selected = true;

	return SPI_RET_OK;
}


static spi_ret_t stm32_spidev_deselect(SpiDev *spidev) {
	Stm32SpiDev *self = (Stm32SpiDev *)spidev->parent;

	if (!self->selected) {
		return SPI_RET_FAILED;
	}
	gpio_set(self->locm3_cs_port, self->locm3_cs_pin);
	self->selected = false;
	self->bus->vmt->unlock(self->bus);

	return SPI_RET_OK;
}


static const struct spidev_vmt stm32_spidev_vmt = {
	.send = stm32_spidev_send,
	.receive = stm32_spidev_receive,
	.exchange = stm32_spidev_exchange,
	.select = stm32_spidev_select,
	.deselect = stm32_spidev_deselect,
};



stm32_spi_ret_t stm32_spidev_init(Stm32SpiDev *self, SpiBus *bus, uint32_t cs_port, uint32_t cs_pin) {
	memset(self, 0, sizeof(Stm32SpiDev));
	self->bus = bus;
	self->locm3_cs_port = cs_port;
	self->locm3_cs_pin = cs_pin;
	gpio_set(self->locm3_cs_port, self->locm3_cs_pin);

	self->dev.parent = self;
	self->dev.vmt = &stm32_spidev_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SPI device initialised"));

	return STM32_SPI_RET_OK;
}


stm32_spi_ret_t stm32_spidev_free(Stm32SpiDev *self) {
	(void)self;
	return STM32_SPI_RET_OK;
}

