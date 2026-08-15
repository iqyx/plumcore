/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keypad implemented using GPIO pins
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/gpio.h>
#include <interfaces/event.h>

#include "gpio-keypad.h"

#define MODULE_NAME "gpio-keypad"


static gpio_keypad_ret_t check_state(GpioKeypad *self) {
	for (struct gpio_keypad_key *key = self->keys; key->input != NULL; key++) {
		bool new_down = false;
		if (key->input == NULL || key->input->vmt->get(key->input, &new_down) != GPIO_RET_OK) {
			continue;
		}
		if (key->invert) {
			new_down = !new_down;
		}
		if (new_down != key->down) {
			if (key->counter >= GPIO_KEYPAD_COUNTER_THRESHOLD) {
				/* The key is observed to be down. */
				key->counter = 0;
				key->down = new_down;

				~ u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("type = %d, code = %d, value = %d"), key->type, key->code, key->down ? 1 : 0);
				struct gpio_keypad_event ev = {
					.type = key->type ? key->type : EV_TYPE_RAW,
					.code = key->code,
					.value = key->down ? 1 : 0
				};
				xQueueSend(self->event_queue, &ev, 0);
			} else {
				key->counter++;
			}
		} else {
			key->counter = 0;
		}
	}

	return GPIO_KEYPAD_RET_OK;
}


static void keypad_task(void *p) {
	GpioKeypad *self = p;


	while (true) {
		check_state(self);

		vTaskDelay(10);
	}
	vTaskDelete(NULL);
}


/**********************************************************************************************************************
 * Event interface implementation
 **********************************************************************************************************************/

static event_ret_t gpio_keypad_event_listen(Event *event, enum event_type *type, enum event_code *code, int32_t *value) {
	GpioKeypad *self = event->parent;
	struct gpio_keypad_event ev = {0};
	xQueueReceive(self->event_queue, &ev, portMAX_DELAY);
	if (type != NULL) {
		*type = ev.type;
	}
	if (code != NULL) {
		*code = ev.code;
	}
	if (value != NULL) {
		*value = ev.value;
	}

	return EV_RET_OK;
}


static const struct event_vmt gpio_keypad_event_vmt = {
	.subscribe = NULL,
	.listen = &gpio_keypad_event_listen,
};


/**********************************************************************************************************************
 * Service implementation
 **********************************************************************************************************************/



gpio_keypad_ret_t gpio_keypad_init(GpioKeypad *self, struct gpio_keypad_key *keys) {
	if (u_assert(self != NULL) ||
	    u_assert(keys != NULL)) {
		return GPIO_KEYPAD_RET_FAILED;
	}
	memset(self, 0, sizeof(GpioKeypad));
	self->keys = keys;

	self->event_queue = xQueueCreate(GPIO_KEYPAD_EVENT_QUEUE_SIZE, sizeof(struct gpio_keypad_event));
	if (self->event_queue == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate event queue"));
		goto err;
	}

	xTaskCreate(keypad_task, "gpio-keypad", configMINIMAL_STACK_SIZE + 192, (void *)self, 2, &(self->keypad_task));
	if (self->keypad_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	self->event.parent = self;
	self->event.vmt = &gpio_keypad_event_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));

	return GPIO_KEYPAD_RET_OK;
err:
	return GPIO_KEYPAD_RET_FAILED;
}


