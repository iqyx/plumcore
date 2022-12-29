/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Driver for SPI NOR flash memories connected over the SPI bus
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/spi.h>
#include <interfaces/flash.h>
#include "spi-flash.h"

#define MODULE_NAME "spi-flash"

/**
 * @brief Flash ID table
 *
 * @todo put into a separate file
 */
#define FLASH_TABLE_ITEM_OPS_COUNT 4
#define _256B 8
#define _4K 12
#define _8K 13
#define _16K 14
#define _32K 15
#define _64K 16
#define _1M 20
#define _2M 21
#define _4M 22
#define _8M 23
#define _16M 24
#define _32M 25

struct __attribute__((__packed__)) flash_table_item  {
	uint32_t id;
	const char *mfg;
	const char *part;
	uint8_t size;
	uint8_t page;
	uint8_t sector;
	uint8_t block;
};


static const struct flash_table_item flash_table[] = {
	{
		.id = 0x00014014,
		.mfg = "Spansion",
		.part = "S25FL208K",
		.size = _1M,
		.page = _256B,
		.sector = _4K,
		.block = _64K
	}, {
		.id = 0x00009d70,
		.mfg = "ISSI",
		.part = "IS25xP016D",
		.size = _2M,
		.page = _256B,
		.sector = _4K,
		.block = _64K
	}, {
		.id = 0x001f8701,
		.mfg = "Adesto",
		.part = "AT25SF321",
		.size = _4M,
		.page = _256B,
		.sector = _4K,
		.block = _64K
	}, {
		.id = 0
	}
};


static spi_flash_ret_t spi_flash_get_id(SpiFlash *self, uint32_t *id) {
	if (u_assert(self != NULL) ||
	    u_assert(id != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	self->spidev->vmt->select(self->spidev);
	self->spidev->vmt->send(self->spidev, &(const uint8_t){0x9f}, 1);
	uint8_t rxbuf[3];
	self->spidev->vmt->receive(self->spidev, rxbuf, 3);
	self->spidev->vmt->deselect(self->spidev);

	*id = (uint32_t)((rxbuf[0] << 16) | (rxbuf[1] << 8) | (rxbuf[2]));

	return SPI_FLASH_RET_OK;
}


static spi_flash_ret_t spi_flash_write_enable(SpiFlash *self, bool enable) {
	if (u_assert(self != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	self->spidev->vmt->select(self->spidev);
	if (enable) {
		self->spidev->vmt->send(self->spidev, &(const uint8_t){0x06}, 1);
	} else {
		self->spidev->vmt->send(self->spidev, &(const uint8_t){0x04}, 1);
	}
	self->spidev->vmt->deselect(self->spidev);

	return SPI_FLASH_RET_OK;
}


static spi_flash_ret_t spi_flash_get_status(SpiFlash *self, uint8_t *status) {
	if (u_assert(self != NULL) ||
	    u_assert(status != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	self->spidev->vmt->select(self->spidev);
	self->spidev->vmt->send(self->spidev, &(const uint8_t){0x05}, 1);
	self->spidev->vmt->receive(self->spidev, status, 1);
	self->spidev->vmt->deselect(self->spidev);

	return SPI_FLASH_RET_OK;
}


static spi_flash_ret_t spi_flash_wait_complete(SpiFlash *self) {
	if (u_assert(self != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	uint8_t sr = 0;
	uint32_t timeout = 0;
	do {
		vTaskDelay(1);
		sr = 0;
		spi_flash_get_status(self, &sr);
		timeout++;
		if (timeout > 2000) {
			return SPI_FLASH_RET_FAILED;
		}

	} while (sr & 0x01);

	return SPI_FLASH_RET_OK;
}


/***************************************************************************************************
 * Flash interface implementation
 ***************************************************************************************************/

static flash_ret_t spi_flash_get_size(Flash *flash, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	SpiFlash *self = (SpiFlash *)flash->parent;

	switch (i) {
		case 0:
			*size = 1UL << flash_table[self->flash_table_index].size;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 1:
			*size = 1UL << flash_table[self->flash_table_index].block;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 2:
			*size = 1UL << flash_table[self->flash_table_index].sector;
			*ops = FLASH_BLOCK_OPS_ERASE;
			break;
		case 3:
			*size = 1UL << flash_table[self->flash_table_index].page;
			*ops = FLASH_BLOCK_OPS_READ + FLASH_BLOCK_OPS_WRITE;
			break;
		default:
			return FLASH_RET_BAD_ARG;
	}
	return FLASH_RET_OK;
}


static flash_ret_t spi_flash_erase(Flash *flash, const size_t addr, size_t len) {
	SpiFlash *self = (SpiFlash *)flash->parent;

	spi_flash_write_enable(self, true);

	self->spidev->vmt->select(self->spidev);
	if (len == (1UL << flash_table[self->flash_table_index].size)) {
		/* Full chip erase */
		self->spidev->vmt->send(self->spidev, (const uint8_t[]){
			0xc7,
		}, 1);
	} else if (len == (1UL << flash_table[self->flash_table_index].block)) {
		/* Block erase */
		self->spidev->vmt->send(self->spidev, (const uint8_t[]){
			0xd8,
			(addr >> 16) & 0xff,
			(addr >> 8) & 0xff,
			addr & 0xff,
		}, 4);
	} else if (len == (1UL << flash_table[self->flash_table_index].sector)) {
		/* Sector erase */
		self->spidev->vmt->send(self->spidev, (const uint8_t[]){
			0x20,
			(addr >> 16) & 0xff,
			(addr >> 8) & 0xff,
			addr & 0xff,
		}, 4);
	} else {
		/* No support for arbitrary length so far or the length is too small.
		 * Fail silently. */
	}
	self->spidev->vmt->deselect(self->spidev);

	if (spi_flash_wait_complete(self) != SPI_FLASH_RET_OK) {
		spi_flash_write_enable(self, false);
		return FLASH_RET_TIMEOUT;
	}
	spi_flash_write_enable(self, false);
	return FLASH_RET_OK;
}


static flash_ret_t spi_flash_write(Flash *flash, const size_t addr, const void *buf, size_t len) {
	SpiFlash *self = (SpiFlash *)flash->parent;

	uint32_t page_size = 1UL << flash_table[self->flash_table_index].page;

	if (len > page_size || len < 1) {
		return FLASH_RET_FAILED;
	}
	/* Programming buffer cannot cross the page boundary. Ie. the buffer must be at most page_size_bytes
	 * long and page_size_bytes aligned. */
	if ((addr % page_size + len) > page_size) {
		return FLASH_RET_FAILED;
	}

	spi_flash_write_enable(self, true);
	self->spidev->vmt->select(self->spidev);

	/* Send the page write command first. */
	self->spidev->vmt->send(self->spidev, (const uint8_t[]){
		0x02,
		(addr >> 16) & 0xff,
		(addr >> 8) & 0xff,
		addr & 0xff,
	}, 4);

	/* Send the data. */
	self->spidev->vmt->send(self->spidev, buf, len);
	self->spidev->vmt->deselect(self->spidev);

	if (spi_flash_wait_complete(self) != SPI_FLASH_RET_OK) {
		return FLASH_RET_TIMEOUT;
	}
	spi_flash_write_enable(self, false);

	return FLASH_RET_OK;
}


static flash_ret_t spi_flash_read(Flash *flash, const size_t addr, void *buf, size_t len) {
	SpiFlash *self = (SpiFlash *)flash->parent;

	if (len < 1) {
		return FLASH_RET_FAILED;
	}
	self->spidev->vmt->select(self->spidev);
	self->spidev->vmt->send(self->spidev, (const uint8_t[]){
		0x03,
		(addr >> 16) & 0xff,
		(addr >> 8) & 0xff,
		addr & 0xff,
	}, 4);
	self->spidev->vmt->receive(self->spidev, buf, len);
	self->spidev->vmt->deselect(self->spidev);

	return FLASH_RET_OK;
}


static const struct flash_vmt spi_flash_vmt = {
	.get_size = spi_flash_get_size,
	.erase = spi_flash_erase,
	.write = spi_flash_write,
	.read = spi_flash_read,
};


static spi_flash_ret_t spi_flash_lookup_id(SpiFlash *self, uint32_t id) {
	uint32_t i = 0;
	while (flash_table[i].id != 0) {
		if (flash_table[i].id == id) {
			self->flash_table_index = i;
			return SPI_FLASH_RET_OK;
		}
		i++;
	}
	return SPI_FLASH_RET_FAILED;
}


spi_flash_ret_t spi_flash_init(SpiFlash *self, SpiDev *spidev) {
	if (u_assert(self != NULL) ||
	    u_assert(spidev != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	memset(self, 0, sizeof(SpiFlash));
	self->spidev = spidev;

	uint32_t id = 0;
	if (spi_flash_get_id(self, &id) != SPI_FLASH_RET_OK || id == 0) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("flash chip detection failed"));
		return SPI_FLASH_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("flash chip detected, id=%08x"), id);

	if (spi_flash_lookup_id(self, id) != SPI_FLASH_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unknown flash, lookup failed"));
		return SPI_FLASH_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO,
		U_LOG_MODULE_PREFIX("manufacturer '%s', part '%s', size %lu K"),
		flash_table[self->flash_table_index].mfg,
		flash_table[self->flash_table_index].part,
		(1 << flash_table[self->flash_table_index].size) / 1024
	);
	u_log(system_log, LOG_TYPE_INFO,
		U_LOG_MODULE_PREFIX("page %lu B, sector %lu K, block %lu K"),
		(1 << flash_table[self->flash_table_index].page),
		(1 << flash_table[self->flash_table_index].sector) / 1024,
		(1 << flash_table[self->flash_table_index].block) / 1024
	);

	self->flash.parent = self;
	self->flash.vmt = &spi_flash_vmt;

	return SPI_FLASH_RET_OK;
}


spi_flash_ret_t spi_flash_free(SpiFlash *self) {
	if (u_assert(self != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	return SPI_FLASH_RET_OK;
}



