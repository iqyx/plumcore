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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "u_assert.h"
#include "interface_led.h"
#include "hal_interface.h"


int32_t interface_led_init(struct interface_led *led) {
	if (u_assert(led != NULL)) {
		return INTERFACE_LED_INIT_FAILED;
	}

	memset(led, 0, sizeof(struct interface_led));
	hal_interface_init(&(led->descriptor), HAL_INTERFACE_TYPE_LED);

	return INTERFACE_LED_INIT_OK;
}


int32_t interface_led_once(struct interface_led *led, uint32_t pattern) {
	if (u_assert(led != NULL)) {
		return INTERFACE_LED_ONCE_FAILED;
	}
	if (led->vmt.once != NULL) {
		return led->vmt.once(led->vmt.context, pattern);
	}
	return INTERFACE_LED_ONCE_FAILED;
}


int32_t interface_led_loop(struct interface_led *led, uint32_t pattern) {
	if (u_assert(led != NULL)) {
		return INTERFACE_LED_LOOP_FAILED;
	}
	if (led->vmt.loop != NULL) {
		return led->vmt.loop(led->vmt.context, pattern);
	}
	return INTERFACE_LED_LOOP_FAILED;
}
