/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 GPIO driver
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <interfaces/gpio.h>

typedef enum {
	STM32_GPIO_RET_OK,
	STM32_GPIO_RET_FAILED,
} stm32_gpio_ret_t;

enum stm32_port {
	STM32_PORTA = (0 << 4),
	STM32_PORTB = (1 << 4),
	STM32_PORTC = (2 << 4),
	STM32_PORTD = (3 << 4),
	STM32_PORTE = (4 << 4),
	STM32_PORTF = (5 << 4),
	STM32_PORTG = (6 << 4),
	STM32_PORTH = (7 << 4),
	STM32_PORTI = (8 << 4),
	STM32_PORTJ = (9 << 4),
	STM32_PORTK = (10 << 4),
};

enum stm32_pin {
	STM32_PIN0 = 0,
	STM32_PIN1 = 1,
	STM32_PIN2 = 2,
	STM32_PIN3 = 3,
	STM32_PIN4 = 4,
	STM32_PIN5 = 5,
	STM32_PIN6 = 6,
	STM32_PIN7 = 7,
	STM32_PIN8 = 8,
	STM32_PIN9 = 9,
	STM32_PIN10 = 10,
	STM32_PIN11 = 11,
	STM32_PIN12 = 12,
	STM32_PIN13 = 13,
	STM32_PIN14 = 14,
	STM32_PIN15 = 15,
};


typedef struct {


} Stm32Gpio;

stm32_gpio_ret_t stm32_gpio_init(
	Stm32Gpio *self,
	enum stm32_port port,
	enum stm32_pin pin,
	enum gpio_mode mode,
	enum gpio_otype otype,
	enum gpio_pull pull
);
stm32_gpio_ret_t stm32_gpio_free(Stm32Gpio *self);
