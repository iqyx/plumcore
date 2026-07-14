/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GPIO LED service
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>
#include "gpio-led.h"

#include <interfaces/gpio.h>
#include <interfaces/led.h>

#define MODULE_NAME "gpio-led"
#define GPIO_LED_TASK_STACK_DEPTH 80


static gpio_led_ret_t create_task(GpioLed *self);

/**********************************************************************************************************************
 * Led interface implementation
 **********************************************************************************************************************/

static led_ret_t led_set(Led *led, led_color_t color) {
	GpioLed *self = led->parent;
	gpio_led_set(self, color);

	return LED_RET_OK;
}


static led_ret_t led_sequence(Led *led, const led_seq_item_t *sequence) {
	GpioLed *self = led->parent;
	self->sequence = sequence;
	create_task(self);

	return LED_RET_OK;
}


static const struct led_vmt gpio_led_vmt = {
	.set = led_set,
	.sequence = led_sequence,
};


/**********************************************************************************************************************
 * Service implementation
 **********************************************************************************************************************/

static void task(void *p) {
	GpioLed *self = p;
	led_seq_item_t seq[16] = {0};

	while (self->task_needed) {
		if (self->sequence) {
			size_t i = 0;
			for (i = 0; i < sizeof(seq) && self->sequence[i]; i++) {
				seq[i] = self->sequence[i];
			}
			seq[i + 1] = LED_SEQ_END;
		}

		for (size_t pos = 0; pos < sizeof(seq) && seq[pos]; pos++) {
			if (seq[pos] & LED_SEQ_SET) {
				gpio_led_set(self, (seq[pos] & 0xffffff00UL) >> 8);
			}
			if (seq[pos] & 0x000000fcUL) {
				uint32_t time_ms = ((seq[pos] & 0x000000fcUL) >> 2) * 16;
				vTaskDelay(pdMS_TO_TICKS(time_ms));
			}
		}

		vTaskDelay(10);

	};
	vTaskDelete(NULL);
}


static gpio_led_ret_t create_task(GpioLed *self) {
	/* Create task once it is needed for the first time. Do not attempt multiple times. */
	if (self->task != NULL) {
		return GPIO_LED_RET_OK;
	}

	/* In case we want to stop it later for some reason. */
	self->task_needed = true;
	xTaskCreate(task, "gpio-led", GPIO_LED_TASK_STACK_DEPTH, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		/* Do not log here to preserve API callee stack. */
		return GPIO_LED_RET_FAILED;
	}

	return GPIO_LED_RET_OK;
}


gpio_led_ret_t gpio_led_init(GpioLed *self, Gpio *r, Gpio *g, Gpio *b) {
	memset(self, 0, sizeof(GpioLed));
	self->r = r;
	self->g = g;
	self->b = b;

	self->led.parent = self;
	self->led.vmt = &gpio_led_vmt;

	return GPIO_LED_RET_OK;
}


gpio_led_ret_t gpio_led_free(GpioLed *self) {
	memset(self, 0, sizeof(GpioLed));

	return GPIO_LED_RET_OK;
}


gpio_led_ret_t gpio_led_invert(GpioLed *self, bool invert) {
	self->invert = invert;

	return GPIO_LED_RET_OK;
}


gpio_led_ret_t gpio_led_set(GpioLed *self, led_color_t color) {

	if (self->r != NULL) {
		self->r->vmt->set(self->r, self->invert != ((color & 0xff0000u) == 0xff0000u));
	}
	if (self->g != NULL) {
		self->g->vmt->set(self->g, self->invert != ((color & 0x00ff00u) == 0x00ff00u));
	}
	if (self->b != NULL) {
		self->b->vmt->set(self->b, self->invert != ((color & 0x0000ffu) == 0x0000ffu));
	}

	return GPIO_LED_RET_OK;
}


