/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keypad implemented using Sensor devices
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

#include <interfaces/sensor.h>
#include <services/stm32-sai/stm32-sai.h>

#include "sensor-keypad.h"

#define MODULE_NAME "sensor-keypad"


static sensor_keypad_ret_t check_state(SensorKeypad *self) {
	for (struct sensor_keypad_key *key = self->keys; key->input != NULL; key++) {
		bool new_down = key->value > key->ema + key->threshold;
		if (new_down != key->down) {
			key->down = new_down;
			u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("value = %f, ema = %f"), key->value, key->ema);


			struct sensor_keypad_event ev = {
				.type = key->code ? key->code : EV_TYPE_RAW,
				.code = key->code,
				.value = key->down ? 1 : 0
			};
			BaseType_t ret = xQueueSend(self->event_queue, &ev, 0);
		}
	}

	return SENSOR_KEYPAD_RET_OK;
}


static sensor_keypad_ret_t recompute_ema(SensorKeypad *self) {
	for (struct sensor_keypad_key *key = self->keys; key->input != NULL; key++) {
		if (key->input->vmt->value_f(key->input, &key->value) != SENSOR_RET_OK) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot read sensor code = %u"), key->code);
			continue;
		}
		if (key->ema == 0.0f) {
			key->ema = key->value;
		} else {
			key->ema = key->ema * (1.0f - SENSOR_KEYPAD_EMA_WEIGHT) + key->value * SENSOR_KEYPAD_EMA_WEIGHT;
		}
	}

	return SENSOR_KEYPAD_RET_OK;
}


static void keypad_task(void *p) {
	SensorKeypad *self = p;


	while (true) {
		recompute_ema(self);
		check_state(self);

		vTaskDelay(50);
	}
	vTaskDelete(NULL);
}


static event_ret_t sensor_keypad_event_listen(Event *event, enum event_type *type, enum event_code *code, int32_t *value) {
	SensorKeypad *self = event->parent;
	struct sensor_keypad_event ev = {0};
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


static const struct event_vmt sensor_keypad_event_vmt = {
	.subscribe = NULL,
	.listen = &sensor_keypad_event_listen,
};


sensor_keypad_ret_t sensor_keypad_init(SensorKeypad *self, struct sensor_keypad_key *keys) {
	if (u_assert(self != NULL) ||
	    u_assert(keys != NULL)) {
		return SENSOR_KEYPAD_RET_FAILED;
	}
	memset(self, 0, sizeof(SensorKeypad));
	self->keys = keys;

	self->event_queue = xQueueCreate(SENSOR_KEYPAD_EVENT_QUEUE_SIZE, sizeof(struct sensor_keypad_event));
	if (self->event_queue == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate event queue"));
		goto err;
	}

	xTaskCreate(keypad_task, "sensor-keypad", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->keypad_task));
	if (self->keypad_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	self->event.parent = self;
	self->event.vmt = &sensor_keypad_event_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));

	return SENSOR_KEYPAD_RET_OK;
err:
	return SENSOR_KEYPAD_RET_FAILED;
}


