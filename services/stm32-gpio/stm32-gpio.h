/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 GPIO driver
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <interfaces/gpio.h>

#if !(defined(STM32L4) || defined(STM32G4) || defined(STM32H7))
#error "stm32-gpio service is not compatible with this MCU family"
#endif

typedef enum {
	STM32_GPIO_RET_OK,
	STM32_GPIO_RET_FAILED,
} stm32_gpio_ret_t;

/**
 * Registers of the GPIO v2 peripheral found in newer STM32s.
 */
#define STM32_GPIO_BASE 0x48000000U
#define STM32_GPIO_MODER(port) MMIO32((port) + 0x00)
#define STM32_GPIO_OTYPER(port) MMIO32((port) + 0x04)
#define STM32_GPIO_OSPEEDR(port) MMIO32((port) + 0x08)
#define STM32_GPIO_PUPDR(port) MMIO32((port) + 0x0c)
#define STM32_GPIO_IDR(port) MMIO32((port) + 0x10)
#define STM32_GPIO_ODR(port) MMIO32((port) + 0x14)
#define STM32_GPIO_BSRR(port) MMIO32((port) + 0x18)
#define STM32_GPIO_LCKR(port) MMIO32((port) + 0x1c)
#define STM32_GPIO_AFRH(port) MMIO32((port) + 0x20)
#define STM32_GPIO_AFRL(port) MMIO32((port) + 0x24)

/**
 * @brief STM32 GPIO peripheral (port) base addresses
 */
enum stm32_port {
	STM32_PORTA = STM32_GPIO_BASE + 0x0000U,
	STM32_PORTB = STM32_GPIO_BASE + 0x0400U,
	STM32_PORTC = STM32_GPIO_BASE + 0x0800U,
	STM32_PORTD = STM32_GPIO_BASE + 0x0c00U,
	STM32_PORTE = STM32_GPIO_BASE + 0x1000U,
	STM32_PORTF = STM32_GPIO_BASE + 0x1400U,
	STM32_PORTG = STM32_GPIO_BASE + 0x1800U,
	STM32_PORTH = STM32_GPIO_BASE + 0x1c00U,
	STM32_PORTI = STM32_GPIO_BASE + 0x2000U,
	STM32_PORTJ = STM32_GPIO_BASE + 0x2400U,
	STM32_PORTK = STM32_GPIO_BASE + 0x2800U,
};


typedef struct {
	enum stm32_port port;
	Gpio pin[16];

} Stm32Gpio;

stm32_gpio_ret_t stm32_gpio_init(Stm32Gpio *self, enum stm32_port port);
stm32_gpio_ret_t stm32_gpio_free(Stm32Gpio *self);
