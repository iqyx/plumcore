/**
 * uMeshFw libopencm3 SPI bus module
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

#ifndef _MODULE_SPIBUS_LOCM3_H_
#define _MODULE_SPIBUS_LOCM3_H_

#include <stdint.h>
#include "hal_module.h"
#include "interface_spibus.h"


struct module_spibus_locm3 {

	struct hal_module_descriptor module;
	struct interface_spibus iface;

	/**
	 * libopencm3 SPI port
	 */
	uint32_t port;

};


int32_t module_spibus_locm3_init(struct module_spibus_locm3 *spibus, const char *name, uint32_t port);
#define MODULE_SPIBUS_LOCM3_INIT_OK 0
#define MODULE_SPIBUS_LOCM3_INIT_FAILED -1

#endif
