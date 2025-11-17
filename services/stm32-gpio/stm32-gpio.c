/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 GPIO driver
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>

#include "stm32-gpio.h"

#define MODULE_NAME "stm32-gpio"


/**********************************************************************************************************************
 * GPIO interface implementation
 **********************************************************************************************************************/

static gpio_ret_t gpio_set(Gpio *gpio, bool state) {
	Stm32Gpio *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	if (state) {
		STM32_GPIO_BSRR(self->port) |= (1 << pin);
	} else {
		STM32_GPIO_BSRR(self->port) |= (1 << (pin + 16));
	}

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_get(Gpio *gpio, bool *state) {
	Stm32Gpio *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	if (state != NULL) {
		if (STM32_GPIO_ODR(self->port) & (1 << pin)) {
			*state = true;
		} else {
			*state = false;
		}
		return GPIO_RET_OK;
	}

	return GPIO_RET_FAILED;
}


static gpio_ret_t gpio_toggle(Gpio *gpio) {
	Stm32Gpio *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	uint32_t state = STM32_GPIO_ODR(self->port);
	STM32_GPIO_BSRR(self->port) = ((state & (1 << pin)) << 16) | (~state & (1 << pin));

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_set_mode(Gpio *gpio, enum gpio_mode mode) {
	Stm32Gpio *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	uint32_t pinmoder = 0;
	switch (mode) {
		case MODE_OUTPUT: pinmoder = 1; break;
		case MODE_ALTERNATE: pinmoder = 2; break;
		case MODE_ANALOG: pinmoder = 3; break;
		default: break;
	}
	STM32_GPIO_MODER(self->port) = (STM32_GPIO_MODER(self->port) & ~(3 << (pin * 2))) | (pinmoder << (pin * 2));

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_set_pinmux(Gpio *gpio, uint32_t mux) {
	Stm32Gpio *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	if (pin < 8) {
		STM32_GPIO_AFRL(self->port) = (STM32_GPIO_AFRL(self->port) & ~(0xf << (pin * 4))) | (mux << (pin * 4));
	} else {
		pin -= 8;
		STM32_GPIO_AFRH(self->port) = (STM32_GPIO_AFRH(self->port) & ~(0xf << (pin * 4))) | (mux << (pin * 4));
	}

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_set_pull(Gpio *gpio, enum gpio_pull pull) {
	Stm32Gpio *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	uint32_t pupd = 0;
	switch (pull) {
		case PULL_UP: pupd = 1; break;
		case PULL_DOWN: pupd = 2; break;
		default: break;
	}
	STM32_GPIO_PUPDR(self->port) = (STM32_GPIO_PUPDR(self->port) & ~(3 << (pin * 2))) | (pupd << (pin * 2));

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_set_otype(Gpio *gpio, enum gpio_otype otype) {
	Stm32Gpio *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	if (otype == OTYPE_OD) {
		STM32_GPIO_OTYPER(self->port) |= (1 << pin);
	} else {
		STM32_GPIO_OTYPER(self->port) &= ~(1 << pin);
	}

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_set_ospeed(Gpio *gpio, enum gpio_ospeed ospeed) {
	Stm32Gpio *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	uint32_t s = 0;
	switch (ospeed) {
		case OSPEED_LOW: break;
		case OSPEED_MEDIUM: s = 1; break;
		case OSPEED_HIGH: s = 2; break;
		case OSPEED_VERYHIGH: s = 3; break;
		default: break;
	}
	STM32_GPIO_OSPEEDR(self->port) = (STM32_GPIO_OSPEEDR(self->port) & ~(3 << (pin * 2))) | (s << (pin * 2));

	return GPIO_RET_OK;
}


static const struct gpio_vmt stm32_gpio_vmt = {
	.set = gpio_set,
	.get = gpio_get,
	.toggle = gpio_toggle,
	.set_mode = gpio_set_mode,
	.set_pinmux = gpio_set_pinmux,
	.set_pull = gpio_set_pull,
	.set_otype = gpio_set_otype,
	.set_ospeed = gpio_set_ospeed,
};


/**********************************************************************************************************************
 * Service implementation
 **********************************************************************************************************************/

stm32_gpio_ret_t stm32_gpio_init(Stm32Gpio *self, enum stm32_port port) {
	memset(self, 0, sizeof(Stm32Gpio));
	self->port = port;

	/* Initialize Gpio pin interfaces. They are effectively the same
	 * but they need to have distinctive addresses. */
	for (int i = 0; i < 16; i++) {
		self->pin[i].parent = self;
		self->pin[i].vmt = &stm32_gpio_vmt;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("16 pins initialized on port %p (GPIO%c)"),
		port,
		(port - STM32_GPIO_BASE) / 0x400u + 'A'
	);

	return STM32_GPIO_RET_OK;
}


stm32_gpio_ret_t stm32_gpio_free(Stm32Gpio *self) {
	(void)self;
	/* Nothing to free. */
	return STM32_GPIO_RET_OK;
}
