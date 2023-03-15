/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Driver for SPI NOR flash memories connected over the SPI bus
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <main.h>

#include <interfaces/spi.h>
#include <interfaces/flash.h>


typedef enum {
	SPI_FLASH_RET_OK = 0,
	SPI_FLASH_RET_FAILED,
	SPI_FLASH_RET_NULL,
} spi_flash_ret_t;


typedef struct {
	bool initialized;

	SpiDev *spidev;
	Flash flash;
	uint32_t flash_table_index;
} SpiFlash;


spi_flash_ret_t spi_flash_init(SpiFlash *self, SpiDev *spidev);
spi_flash_ret_t spi_flash_free(SpiFlash *self);
