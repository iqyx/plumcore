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
#include <main.h>


typedef enum {
	STM32_SPI_RET_OK = 0,
	STM32_SPI_RET_FAILED,
} stm32_spi_ret_t;

typedef struct stm32_spi_bus {
	SpiBus bus;

	uint32_t locm3_spi;

	SemaphoreHandle_t bus_lock;
} Stm32SpiBus;

typedef struct stm32_spi_dev {
	SpiBus *bus;
	SpiDev dev;

	uint32_t locm3_cs_port;
	uint32_t locm3_cs_pin;

	bool selected;
} Stm32SpiDev;


stm32_spi_ret_t stm32_spibus_init(Stm32SpiBus *self, uint32_t locm3_spi);
stm32_spi_ret_t stm32_spibus_free(Stm32SpiBus *self);

stm32_spi_ret_t stm32_spidev_init(Stm32SpiDev *self, SpiBus *bus, uint32_t cs_port, uint32_t cs_pin);
stm32_spi_ret_t stm32_spidev_free(Stm32SpiDev *self);

