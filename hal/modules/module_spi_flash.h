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

#ifndef _SPI_FLASH_H_
#define _SPI_FLASH_H_

#include <stdint.h>
#include <stdbool.h>

#include "interface_spidev.h"
#include "interface_flash.h"

struct module_spi_flash {
	struct interface_flash iface;
	struct interface_spidev *spidev;
};


int32_t module_spi_flash_init(struct module_spi_flash *flash, const char *name, struct interface_spidev *spidev);
#define MODULE_SPI_FLASH_INIT_OK 0
#define MODULE_SPI_FLASH_INIT_FAILED -1

int32_t module_spi_flash_free(struct module_spi_flash *flash);
#define MODULE_SPI_FLASH_FREE_OK 0
#define MODULE_SPI_FLASH_FREE_FAILED -1



#endif



