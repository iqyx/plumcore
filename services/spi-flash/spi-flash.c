/*
 * Driver for SPI NOR flash memories connected over the SPI bus
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include "spi-flash.h"
#include "interface_spidev.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "spi-flash"


static spi_flash_ret_t spi_flash_get_id(SpiFlash *self, uint32_t *id) {
	if (u_assert(self != NULL) ||
	    u_assert(id != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	interface_spidev_select(self->spi_dev);
	interface_spidev_send(self->spi_dev, &(const uint8_t){0x9f}, 1);
	uint8_t rxbuf[3];
	interface_spidev_receive(self->spi_dev, rxbuf, 3);
	interface_spidev_deselect(self->spi_dev);

	*id = (uint32_t)((rxbuf[0] << 16) | (rxbuf[1] << 8) | (rxbuf[2]));

	return SPI_FLASH_RET_OK;
}


static iflash_ret_t flash_get_info(void *context, struct iflash_info *info) {
	if (u_assert(context != NULL) ||
	    u_assert(info != NULL)) {
		return IFLASH_RET_NULL;
	}
	SpiFlash *self = (SpiFlash *)context;
	if (self->initialized == false) {
		return SPI_FLASH_RET_FAILED;
	}

	/* The flash chip is already detected, ID is read and
	 * the flash_info struct is filled with the correct data.
	 * Just copy it to the output. */
	memcpy(info, &self->flash_info, sizeof(struct iflash_info));

	return IFLASH_RET_OK;
}


static spi_flash_ret_t spi_flash_write_enable(SpiFlash *self, bool enable) {
	if (u_assert(self != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	interface_spidev_select(self->spi_dev);
	if (enable) {
		interface_spidev_send(self->spi_dev, &(const uint8_t){0x06}, 1);
	} else {
		interface_spidev_send(self->spi_dev, &(const uint8_t){0x04}, 1);
	}
	interface_spidev_deselect(self->spi_dev);

	return SPI_FLASH_RET_OK;
}


static spi_flash_ret_t spi_flash_get_status(SpiFlash *self, uint8_t *status) {
	if (u_assert(self != NULL) ||
	    u_assert(status != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	interface_spidev_select(self->spi_dev);
	interface_spidev_send(self->spi_dev, &(const uint8_t){0x05}, 1);
	interface_spidev_receive(self->spi_dev, status, 1);
	interface_spidev_deselect(self->spi_dev);

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


static iflash_ret_t flash_erase(void *context, const uint64_t addr, uint32_t sector_size_index) {
	if (u_assert(context != NULL)) {
		return IFLASH_RET_NULL;
	}
	SpiFlash *self = (SpiFlash *)context;
	if (self->initialized == false) {
		return IFLASH_RET_FAILED;
	}

	spi_flash_write_enable(self, true);

	interface_spidev_select(self->spi_dev);
	switch (sector_size_index) {
		case 0:
			/* Size 0 sector is usually called "Sector" in the flash chip datasheets.
			 * It is the smallest erasable block in the memory. */
			interface_spidev_send(self->spi_dev, (const uint8_t[]){
				0x20,
				(addr >> 16) & 0xff,
				(addr >> 8) & 0xff,
				addr & 0xff,
			}, 4);
			break;

		case 2:
			/* Size 2 sector is sometimes called a "block". */
			interface_spidev_send(self->spi_dev, (const uint8_t[]){
				0xd8,
				(addr >> 16) & 0xff,
				(addr >> 8) & 0xff,
				addr & 0xff,
			}, 4);
			break;
		case 3:
			/* This is usually used as a full chip erase (the sector size
			 * is equal to the memory size. */
			interface_spidev_send(self->spi_dev, (const uint8_t[]){
				0xc7,
			}, 1);
			break;

		default:
			interface_spidev_deselect(self->spi_dev);
			return IFLASH_RET_FAILED;
	}
	interface_spidev_deselect(self->spi_dev);

	if (spi_flash_wait_complete(self) != SPI_FLASH_RET_OK) {
		spi_flash_write_enable(self, false);
		return IFLASH_RET_TIMEOUT;
	}

	spi_flash_write_enable(self, false);
	return IFLASH_RET_OK;
}


static iflash_ret_t flash_write(void *context, const uint64_t addr, const uint8_t *buf, size_t len) {
	if (u_assert(context != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len > 0)) {
		return IFLASH_RET_NULL;
	}
	SpiFlash *self = (SpiFlash *)context;
	if (self->initialized == false) {
		return IFLASH_RET_FAILED;
	}
	if (len > self->flash_info.page_size_bytes || len < 1) {
		return IFLASH_RET_FAILED;
	}
	/* Programming buffer cannot cross the page boundary. Ie. the buffer must be at most page_size_bytes
	 * long and page_size_bytes aligned. */
	if ((addr % self->flash_info.page_size_bytes + len) > self->flash_info.page_size_bytes) {
		return SPI_FLASH_RET_FAILED;
	}

	spi_flash_write_enable(self, true);

	interface_spidev_select(self->spi_dev);
	interface_spidev_send(self->spi_dev, (const uint8_t[]){
		0x02,
		(addr >> 16) & 0xff,
		(addr >> 8) & 0xff,
		addr & 0xff,
	}, 4);
	interface_spidev_send(self->spi_dev, buf, len);
	interface_spidev_deselect(self->spi_dev);

	if (spi_flash_wait_complete(self) != SPI_FLASH_RET_OK) {
		return IFLASH_RET_TIMEOUT;
	}
	spi_flash_write_enable(self, false);

	return IFLASH_RET_OK;
}


static iflash_ret_t flash_read(void *context, const uint64_t addr, uint8_t *buf, size_t len) {
	if (u_assert(context != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len > 0)) {
		return IFLASH_RET_NULL;
	}
	SpiFlash *self = (SpiFlash *)context;
	if (self->initialized == false) {
		return IFLASH_RET_FAILED;
	}
	if (len < 1) {
		return IFLASH_RET_FAILED;
	}

	interface_spidev_select(self->spi_dev);
	interface_spidev_send(self->spi_dev, (const uint8_t[]){
		0x03,
		(addr >> 16) & 0xff,
		(addr >> 8) & 0xff,
		addr & 0xff,
	}, 4);
	interface_spidev_receive(self->spi_dev, buf, len);
	interface_spidev_deselect(self->spi_dev);

	return IFLASH_RET_OK;
}


static spi_flash_ret_t spi_flash_lookup_id(SpiFlash *self, uint32_t id, struct iflash_info *info) {
	if (u_assert(self != NULL) ||
	    u_assert(info != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	memset(info, 0, sizeof(struct iflash_info));
	switch (id) {
		case 0x00014014:
			info->size_bytes = 1024 * 1024;
			info->page_size_bytes = 256;
			info->sector_size_bytes[0] = 4 * 1024; /* sector */
			info->sector_size_bytes[2] = 64 * 1024; /* block */
			info->sector_size_bytes[3] = 1024 * 1024; /* chip */
			info->manufacturer = "Spansion";
			info->part = "S25FL208K";
			break;

		case 0x00009d70:
			info->size_bytes = 2 * 1024 * 1024;
			info->page_size_bytes = 256;
			info->sector_size_bytes[0] = 4 * 1024; /* sector */
			info->sector_size_bytes[1] = 32 * 1024; /* half block */
			info->sector_size_bytes[2] = 64 * 1024; /* block */
			info->sector_size_bytes[3] = 2 * 1024 * 1024; /* chip */
			info->manufacturer = "ISSI";
			info->part = "IS25xP016D";
			break;

		default:
			return SPI_FLASH_RET_FAILED;
	}

	return SPI_FLASH_RET_OK;
}


spi_flash_ret_t spi_flash_init(SpiFlash *self, struct interface_spidev *spi_dev) {
	if (u_assert(self != NULL) ||
	    u_assert(spi_dev != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	memset(self, 0, sizeof(SpiFlash));
	self->spi_dev = spi_dev;

	uint32_t id = 0;
	if (spi_flash_get_id(self, &id) != SPI_FLASH_RET_OK || id == 0) {
		u_log(system_log, LOG_TYPE_ERROR,
			U_LOG_MODULE_PREFIX("flash chip detection failed, id=%08x"),
			id
		);
		return SPI_FLASH_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("flash chip detected, id=%08x"), id);

	if (spi_flash_lookup_id(self, id, &self->flash_info) != SPI_FLASH_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unknown flash, lookup failed"));
		return SPI_FLASH_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO,
		U_LOG_MODULE_PREFIX("manufacturer %s, part %s, size %luK"),
		self->flash_info.manufacturer,
		self->flash_info.part,
		(uint32_t)(self->flash_info.size_bytes / 1024)
	);
	u_log(system_log, LOG_TYPE_INFO,
		U_LOG_MODULE_PREFIX("page %luB, sector sizes %luK, %luK, %luK, %luK"),
		self->flash_info.page_size_bytes,
		self->flash_info.sector_size_bytes[0] / 1024,
		self->flash_info.sector_size_bytes[1] / 1024,
		self->flash_info.sector_size_bytes[2] / 1024,
		self->flash_info.sector_size_bytes[3] / 1024
	);

	iflash_init(&self->iflash);
	self->iflash.vmt.context = (void *)self;
	self->iflash.vmt.get_info = flash_get_info;
	self->iflash.vmt.erase = flash_erase;
	self->iflash.vmt.write = flash_write;
	self->iflash.vmt.read = flash_read;

	self->initialized = true;
	return SPI_FLASH_RET_OK;
}


spi_flash_ret_t spi_flash_free(SpiFlash *self) {
	if (u_assert(self != NULL)) {
		return SPI_FLASH_RET_NULL;
	}

	/* Nothing to free. */
	self->initialized = false;

	return SPI_FLASH_RET_OK;
}


IFlash *spi_flash_interface(SpiFlash *self) {
	if (u_assert(self != NULL)) {
		return NULL;
	}

	return &self->iflash;
}

