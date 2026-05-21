/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 GPIO driver
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <interfaces/gpio.h>

typedef enum {
	STM32_GPIO_RET_OK,
	STM32_GPIO_RET_FAILED,
} stm32_gpio_ret_t;


typedef struct {
	void *port;
	Gpio pin[16];

} Stm32Gpio;

stm32_gpio_ret_t stm32_gpio_init(Stm32Gpio *self, void *port_base);
stm32_gpio_ret_t stm32_gpio_free(Stm32Gpio *self);
