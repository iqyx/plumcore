/**
 * uMeshFw SPI Flash module
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libopencm3/stm32/gpio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "hal_module.h"
#include "module_spi_flash.h"
#include "interface_spibus.h"
#include "interface_flash.h"


static int32_t module_spi_flash_get_id(void *context, uint32_t *id) {
	if (u_assert(context != NULL && id != NULL)) {
		return INTERFACE_FLASH_GET_ID_FAILED;
	}
	struct module_spi_flash *flash = (struct module_spi_flash *)context;

	interface_spidev_select(flash->spidev);
	interface_spidev_send(flash->spidev, &(const uint8_t){0x9f}, 1);
	uint8_t rxbuf[3];
	interface_spidev_receive(flash->spidev, rxbuf, 3);
	interface_spidev_deselect(flash->spidev);

	*id = (uint32_t)((rxbuf[0] << 16) | (rxbuf[1] << 8) | (rxbuf[2]));

	return INTERFACE_FLASH_GET_ID_OK;
}


static int32_t module_spi_flash_get_info(void *context, struct interface_flash_info *info) {
	if (u_assert(context != NULL && info != NULL)) {
		return INTERFACE_FLASH_GET_INFO_FAILED;
	}

	if (module_spi_flash_get_id(context, &(info->id)) != INTERFACE_FLASH_GET_ID_OK) {
		return INTERFACE_FLASH_GET_INFO_FAILED;
	}

	/* Spansion S25FL208K */
	if (info->id == 0x00014014) {
		info->capacity = 1024 * 1024;
		info->page_size = 256;
		info->sector_size = 4096;
		info->block_size = 65536;
		info->manufacturer = "Spansion";
		info->part = "S25FL208K";
		return INTERFACE_FLASH_GET_INFO_OK;
	}

	return INTERFACE_FLASH_GET_INFO_UNKNOWN;
}


#define MODULE_SPI_FLASH_WRITE_ENABLE_OK 0
#define MODULE_SPI_FLASH_WRITE_ENABLE_FAILED -1
static int32_t module_spi_flash_write_enable(struct module_spi_flash *flash, bool ena) {
	if (u_assert(flash != NULL)) {
		return MODULE_SPI_FLASH_WRITE_ENABLE_FAILED;
	}

	interface_spidev_select(flash->spidev);
	if (ena) {
		interface_spidev_send(flash->spidev, &(const uint8_t){0x06}, 1);
	} else {
		interface_spidev_send(flash->spidev, &(const uint8_t){0x04}, 1);
	}
	interface_spidev_deselect(flash->spidev);

	return MODULE_SPI_FLASH_WRITE_ENABLE_OK;
}


#define MODULE_SPI_FLASH_GET_STATUS_OK 0
#define MODULE_SPI_FLASH_GET_STATUS_FAILED -1
static int32_t module_spi_flash_get_status(struct module_spi_flash *flash, uint8_t *status) {
	if (u_assert(flash != NULL && status == NULL)) {
		return MODULE_SPI_FLASH_GET_STATUS_FAILED;
	}

	interface_spidev_select(flash->spidev);
	interface_spidev_send(flash->spidev, &(const uint8_t){0x05}, 1);
	interface_spidev_receive(flash->spidev, status, 1);
	interface_spidev_deselect(flash->spidev);

	return MODULE_SPI_FLASH_GET_STATUS_OK;
}


#define MODULE_SPI_FLASH_WAIT_COMPLETE_OK 0
#define MODULE_SPI_FLASH_WAIT_COMPLETE_FAILED -1
static int32_t module_spi_flash_wait_complete(struct module_spi_flash *flash) {
	if (u_assert(flash != NULL)) {
		return MODULE_SPI_FLASH_WAIT_COMPLETE_FAILED;
	}

	uint8_t sr;
	uint32_t timeout = 0;
	do {
		vTaskDelay(1);
		module_spi_flash_get_status(flash, &sr);
		timeout++;
		if (timeout > 2000) {
			return MODULE_SPI_FLASH_WAIT_COMPLETE_FAILED;
		}

	} while (sr & 0x01);

	return MODULE_SPI_FLASH_WAIT_COMPLETE_OK;
}


static int32_t module_spi_flash_chip_erase(void *context) {
	if (u_assert(context != NULL)) {
		return INTERFACE_FLASH_CHIP_ERASE_FAILED;
	}
	struct module_spi_flash *flash = (struct module_spi_flash *)context;

	(void)flash;

	return INTERFACE_FLASH_CHIP_ERASE_OK;
}


static int32_t module_spi_flash_block_erase(void *context, const uint32_t addr) {
	if (u_assert(context != NULL)) {
		return INTERFACE_FLASH_BLOCK_ERASE_FAILED;
	}
	struct module_spi_flash *flash = (struct module_spi_flash *)context;

	module_spi_flash_write_enable(flash, true);

	interface_spidev_select(flash->spidev);
	interface_spidev_send(flash->spidev, (const uint8_t[]){
		0xd8,
		(addr >> 16) & 0xff,
		(addr >> 8) & 0xff,
		addr & 0xff,
	}, 4);
	interface_spidev_deselect(flash->spidev);

	if (module_spi_flash_wait_complete(flash) != MODULE_SPI_FLASH_WAIT_COMPLETE_OK) {
		return INTERFACE_FLASH_BLOCK_ERASE_FAILED;
	}
	module_spi_flash_write_enable(flash, false);

	return INTERFACE_FLASH_BLOCK_ERASE_OK;
}


static int32_t module_spi_flash_sector_erase(void *context, const uint32_t addr) {
	if (u_assert(context != NULL)) {
		return INTERFACE_FLASH_SECTOR_ERASE_FAILED;
	}
	struct module_spi_flash *flash = (struct module_spi_flash *)context;

	module_spi_flash_write_enable(flash, true);

	interface_spidev_select(flash->spidev);
	interface_spidev_send(flash->spidev, (const uint8_t[]){
		0x20,
		(addr >> 16) & 0xff,
		(addr >> 8) & 0xff,
		addr & 0xff,
	}, 4);
	interface_spidev_deselect(flash->spidev);

	if (module_spi_flash_wait_complete(flash) != MODULE_SPI_FLASH_WAIT_COMPLETE_OK) {
		return INTERFACE_FLASH_BLOCK_ERASE_FAILED;
	}
	module_spi_flash_write_enable(flash, false);

	return INTERFACE_FLASH_SECTOR_ERASE_OK;
}


static int32_t module_spi_flash_page_write(void *context, const uint32_t addr, const uint8_t *data, const uint32_t len) {
	if (u_assert(context != NULL && data != NULL && len > 0)) {
		return INTERFACE_FLASH_PAGE_WRITE_FAILED;
	}
	struct module_spi_flash *flash = (struct module_spi_flash *)context;

	module_spi_flash_write_enable(flash, true);

	interface_spidev_select(flash->spidev);
	interface_spidev_send(flash->spidev, (const uint8_t[]){
		0x02,
		(addr >> 16) & 0xff,
		(addr >> 8) & 0xff,
		addr & 0xff,
	}, 4);
	interface_spidev_send(flash->spidev, data, len);
	interface_spidev_deselect(flash->spidev);

	if (module_spi_flash_wait_complete(flash) != MODULE_SPI_FLASH_WAIT_COMPLETE_OK) {
		return INTERFACE_FLASH_BLOCK_ERASE_FAILED;
	}
	module_spi_flash_write_enable(flash, false);

	return INTERFACE_FLASH_PAGE_WRITE_OK;
}


static int32_t module_spi_flash_page_read(void *context, const uint32_t addr, uint8_t *data, const uint32_t len) {
	if (u_assert(context != NULL && data != NULL && len > 0)) {
		return INTERFACE_FLASH_PAGE_READ_FAILED;
	}
	struct module_spi_flash *flash = (struct module_spi_flash *)context;

	interface_spidev_select(flash->spidev);
	interface_spidev_send(flash->spidev, (const uint8_t[]){
		0x03,
		(addr >> 16) & 0xff,
		(addr >> 8) & 0xff,
		addr & 0xff,
	}, 4);
	interface_spidev_receive(flash->spidev, data, len);
	interface_spidev_deselect(flash->spidev);

	return INTERFACE_FLASH_PAGE_READ_OK;
}


int32_t module_spi_flash_init(struct module_spi_flash *flash, const char *name, struct interface_spidev *spidev) {
	if (u_assert(flash != NULL && spidev != NULL)) {
		return MODULE_SPI_FLASH_INIT_FAILED;
	}

	memset(flash, 0, sizeof(struct module_spi_flash));
	hal_module_descriptor_init(&(flash->module), name);
	hal_module_descriptor_set_shm(&(flash->module), (void *)flash, sizeof(struct module_spi_flash));

	flash->spidev = spidev;

	/* Initialize flash interface. */
	interface_flash_init(&(flash->iface));
	flash->iface.vmt.context = (void *)flash;
	flash->iface.vmt.get_id = module_spi_flash_get_id;
	flash->iface.vmt.get_info = module_spi_flash_get_info;
	flash->iface.vmt.chip_erase = module_spi_flash_chip_erase;
	flash->iface.vmt.block_erase = module_spi_flash_block_erase;
	flash->iface.vmt.sector_erase = module_spi_flash_sector_erase;
	flash->iface.vmt.page_write = module_spi_flash_page_write;
	flash->iface.vmt.page_read = module_spi_flash_page_read;

	/* Try to communicate and detect flash memory. */
	struct interface_flash_info info;
	int32_t ret = module_spi_flash_get_info(flash, &info);
	if (ret != INTERFACE_FLASH_GET_INFO_OK) {
		/* Communication failed or unknown flash memory detected. */
		if (ret == INTERFACE_FLASH_GET_INFO_UNKNOWN) {
			u_log(system_log, LOG_TYPE_WARN, "%s: unknown flash chip detected (id = 0x%08x)", flash->module.name, info.id);
		} else {
			u_log(system_log, LOG_TYPE_WARN, "%s: flash chip detection failed", flash->module.name);
		}
		return MODULE_SPI_FLASH_INIT_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO,
		"%s: flash detected %s %s, size %u bytes",
		flash->module.name,
		info.manufacturer,
		info.part,
		info.capacity
	);

	return MODULE_SPI_FLASH_INIT_OK;
}

