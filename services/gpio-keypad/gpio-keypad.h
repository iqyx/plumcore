/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Keypad implemented using GPIO pins
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>

#include <interfaces/gpio.h>
#include <interfaces/event.h>


#define GPIO_KEYPAD_EVENT_QUEUE_SIZE 4
#define GPIO_KEYPAD_COUNTER_THRESHOLD 3

typedef enum  {
	GPIO_KEYPAD_RET_OK = 0,
	GPIO_KEYPAD_RET_FAILED,
} gpio_keypad_ret_t;

struct gpio_keypad_event {
	enum event_type type;
	enum event_code code;
	int32_t value;

};

struct gpio_keypad_key {
	Gpio *input;
	enum event_type type;
	enum event_code code;
	bool invert;

	/* Runtime data */
	bool down;
	int counter;
};

typedef struct gpio_keypad {
	struct gpio_keypad_key *keys;
	TaskHandle_t keypad_task;
	QueueHandle_t event_queue;

	Event event;
} GpioKeypad;


gpio_keypad_ret_t gpio_keypad_init(GpioKeypad *self, struct gpio_keypad_key *keys);

