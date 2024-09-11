/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 GPIO driver
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include <main.h>

#include "stm32-gpio.h"

#define MODULE_NAME "stm32-gpio"


stm32_gpio_ret_t stm32_gpio_init(
	Stm32Gpio *self,
	enum stm32_port port,
	enum stm32_pin pin,
	enum gpio_mode mode,
	enum gpio_otype otype,
	enum gpio_pull pull
) {


	return STM32_GPIO_RET_OK;
}

stm32_gpio_ret_t stm32_gpio_free(Stm32Gpio *self) {


	return STM32_GPIO_RET_OK;
}
