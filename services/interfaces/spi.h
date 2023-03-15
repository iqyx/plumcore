/* SPDX-License-Identifier: BSD-2-Clause
 *
 * SPI bus/device interface
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	SPI_RET_OK = 0,
	SPI_RET_FAILED,
	SPI_RET_NOT_AVAILABLE,
} spi_ret_t;

typedef struct spibus SpiBus;

struct spibus_vmt {
	spi_ret_t (*send)(SpiBus *self, const uint8_t *txbuf, size_t txlen);
	spi_ret_t (*receive)(SpiBus *self, uint8_t *rxbuf, size_t rxlen);
	spi_ret_t (*exchange)(SpiBus *self, const uint8_t *txbuf, uint8_t *rxbuf, size_t len);
	spi_ret_t (*lock)(SpiBus *self);
	spi_ret_t (*unlock)(SpiBus *self);
	spi_ret_t (*set_sck_freq)(SpiBus *self, uint32_t freq_hz);
	spi_ret_t (*set_mode)(SpiBus *self, uint8_t cpol, uint8_t cpha);
};

typedef struct spibus {
	const struct spibus_vmt *vmt;
	void *parent;
} SpiBus;

typedef struct spidev SpiDev;

struct spidev_vmt {
	spi_ret_t (*send)(SpiDev *self, const uint8_t *txbuf, size_t txlen);
	spi_ret_t (*receive)(SpiDev *self, uint8_t *rxbuf, size_t rxlen);
	spi_ret_t (*exchange)(SpiDev *self, const uint8_t *txbuf, uint8_t *rxbuf, size_t len);
	spi_ret_t (*select)(SpiDev *self);
	spi_ret_t (*deselect)(SpiDev *self);
};

typedef struct spidev {
	const struct spidev_vmt *vmt;
	void *parent;
} SpiDev;
