/**
 * uMeshFw HAL LED interface
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

#ifndef _IFACE_LED_H_
#define _IFACE_LED_H_

#include <stdint.h>
#include "hal_interface.h"

struct interface_led_vmt {
	int32_t (*once)(void *context, uint32_t pattern);
	int32_t (*loop)(void *context, uint32_t pattern);
	void *context;
};

struct interface_led {
	struct hal_interface_descriptor descriptor;
	/* TODO: interface descriptor */

	struct interface_led_vmt vmt;

};


int32_t interface_led_init(struct interface_led *led);
#define INTERFACE_LED_INIT_OK 0
#define INTERFACE_LED_INIT_FAILED -1

int32_t interface_led_once(struct interface_led *led, uint32_t pattern);
#define INTERFACE_LED_ONCE_OK 0
#define INTERFACE_LED_ONCE_FAILED -1

int32_t interface_led_loop(struct interface_led *led, uint32_t pattern);
#define INTERFACE_LED_LOOP_OK 0
#define INTERFACE_LED_LOOP_FAILED -1


#endif
