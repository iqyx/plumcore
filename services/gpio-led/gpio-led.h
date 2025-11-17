/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GPIO LED service
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <interfaces/led.h>
#include <interfaces/gpio.h>

typedef enum {
	GPIO_LED_RET_OK = 0,
	GPIO_LED_RET_FAILED,
} gpio_led_ret_t;

typedef struct gpio_led {
	Led led;
	Gpio *r;
	Gpio *g;
	Gpio *b;
	bool invert;

	TaskHandle_t task;
	bool task_needed;

	volatile led_color_t last_color;
	volatile const led_seq_item_t *sequence;
	uint32_t seq_pos;
} GpioLed;


gpio_led_ret_t gpio_led_init(GpioLed *self, Gpio *r, Gpio *g, Gpio *b);
gpio_led_ret_t gpio_led_free(GpioLed *self);
gpio_led_ret_t gpio_led_invert(GpioLed *self, bool invert);
gpio_led_ret_t gpio_led_set(GpioLed *self, led_color_t color);

