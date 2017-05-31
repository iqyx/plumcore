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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libopencm3/stm32/gpio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "hal_module.h"
#include "interface_led.h"
#include "module_led.h"


static int32_t module_led_once(void *context, uint32_t pattern) {
	if (u_assert(context != NULL)) {
		return INTERFACE_LED_ONCE_FAILED;
	}
	struct module_led *led = (struct module_led *)context;

	led->pattern = pattern;
	led->loop = 0;
	led->once = 1;

	return INTERFACE_LED_ONCE_OK;
}


static int32_t module_led_loop(void *context, uint32_t pattern) {
	if (u_assert(context != NULL)) {
		return INTERFACE_LED_LOOP_FAILED;
	}
	struct module_led *led = (struct module_led *)context;

	led->pattern = pattern;
	led->loop = 1;

	return INTERFACE_LED_LOOP_OK;
}


static void led_task(void *p) {
	struct module_led *led = (struct module_led *)p;

	led->pos = 8;
	led->once = 0;
	gpio_clear(led->port, led->pin);
	while (1) {

		uint8_t len;
		if (led->pos >= 8) {
			len = 0;
		} else {
			len = ((led->pattern >> (led->pos * 4)) & 15);
		}

		if (len > 0) {
			if (led->pos % 2) {
				gpio_clear(led->port, led->pin);
			} else {
				gpio_set(led->port, led->pin);
			}
			vTaskDelay(len * 50);
		} else {
			if (led->loop || led->once) {
				led->pos = 0;
			}
			led->once = 0;
			vTaskDelay(50);
			continue;
		}

		led->pos++;
	}
}


int32_t module_led_init(struct module_led *led, const char *name) {
	if (u_assert(led != NULL)) {
		return MODULE_LED_INIT_FAILED;
	}

	memset(led, 0, sizeof(struct module_led));
	hal_module_descriptor_init(&(led->module), name);
	hal_module_descriptor_set_shm(&(led->module), (void *)led, sizeof(struct module_led));

	/* Initialize LED interface. */
	interface_led_init(&(led->iface));
	led->iface.vmt.context = (void *)led;
	led->iface.vmt.once = module_led_once;
	led->iface.vmt.loop = module_led_loop;

	/* TODO: init process here */
	led->pattern = 0;
	led->pos = 0;
	led->once = 0;
	led->loop = 0;

	xTaskCreate(led_task, "led_task", configMINIMAL_STACK_SIZE, (void *)led, 1, NULL);

	u_log(system_log, LOG_TYPE_INFO, "%s: LED module initialized", led->module.name);

	return MODULE_LED_INIT_OK;
}


int32_t module_led_set_port(struct module_led *led, uint32_t port, uint32_t pin) {
	if (u_assert(led != NULL)) {
		return MODULE_LED_SET_PORT_FAILED;
	}

	led->port = port;
	led->pin = pin;

	return MODULE_LED_SET_PORT_OK;
}


