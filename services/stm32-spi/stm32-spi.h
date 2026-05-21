/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 SPI driver service
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <interfaces/spi.h>
#include <interfaces/gpio.h>
#include <main.h>


enum stm32_spi_per_type {
	STM32_SPI_PER_TYPE_SPI = 0,
	STM32_SPI_PER_TYPE_UART,
};

typedef enum {
	STM32_SPI_RET_OK = 0,
	STM32_SPI_RET_FAILED,
} stm32_spi_ret_t;

typedef struct stm32_spi_bus {
	SpiBus bus;

	void *base;
	enum stm32_spi_per_type per_type;

	SemaphoreHandle_t bus_lock;
	SemaphoreHandle_t eot_wait;
} Stm32SpiBus;

typedef struct stm32_spi_dev {
	SpiBus *bus;
	SpiDev dev;

	Gpio *cs;

	bool selected;
} Stm32SpiDev;


stm32_spi_ret_t stm32_spibus_init(Stm32SpiBus *self, void *base, enum stm32_spi_per_type per_type);
stm32_spi_ret_t stm32_spibus_free(Stm32SpiBus *self);
stm32_spi_ret_t stm32_spibus_irq_handler(Stm32SpiBus *self);

stm32_spi_ret_t stm32_spidev_init(Stm32SpiDev *self, SpiBus *bus, Gpio *cs);
stm32_spi_ret_t stm32_spidev_free(Stm32SpiDev *self);

