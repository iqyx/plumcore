/**
 * uMeshFw LED module
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

#ifndef _MODULE_LED_H_
#define _MODULE_LED_H_

#include <stdint.h>
#include "hal_module.h"
#include "interface_led.h"

struct module_led {

	struct hal_module_descriptor module;
	struct interface_led iface;

	uint32_t port;
	uint32_t pin;

	uint32_t pattern;
	uint8_t pos;
	uint8_t once;
	uint8_t loop;

};



int32_t module_led_init(struct module_led *led, const char *name);
#define MODULE_LED_INIT_OK 0
#define MODULE_LED_INIT_FAILED -1

int32_t module_led_set_port(struct module_led *led, uint32_t port, uint32_t pin);
#define MODULE_LED_SET_PORT_OK 0
#define MODULE_LED_SET_PORT_FAILED -1



#endif

