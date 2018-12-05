/**
 * uMeshFw SPI device module, libopencm3 GPIO chip selects
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

#ifndef _MODULE_SPIDEV_LOCM3_H_
#define _MODULE_SPIDEV_LOCM3_H_

#include <stdint.h>
#include "interface_spibus.h"
#include "interface_spidev.h"


struct module_spidev_locm3 {
	struct interface_spidev iface;
	struct interface_spibus *spibus;

	uint32_t cs_port;
	uint32_t cs_pin;
};


int32_t module_spidev_locm3_init(struct module_spidev_locm3 *spidev, const char *name, struct interface_spibus *spibus, uint32_t cs_port, uint32_t cs_pin);
#define MODULE_SPIDEV_LOCM3_INIT_OK 0
#define MODULE_SPIDEV_LOCM3_INIT_FAILED -1

int32_t module_spidev_locm3_free(struct module_spidev_locm3 *spidev);
#define MODULE_SPIDEV_LOCM3_FREE_OK 0
#define MODULE_SPIDEV_LOCM3_FREE_FAILED -1







#endif

