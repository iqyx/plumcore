/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Driver for GPIO over PCAL6408A expander
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <main.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/gpio.h>

typedef enum  {
	PCAL6408A_GPIO_RET_OK = 0,
	PCAL6408A_GPIO_RET_FAILED,
} pcal6408a_gpio_ret_t;

typedef struct pcal6408a_gpio {
	I2cBus *i2c;

	Gpio pin[8];

	uint8_t reg_ipr;
	uint8_t reg_opr;
	uint8_t reg_cr;

} Pcal6408A;


pcal6408a_gpio_ret_t pcal6408a_gpio_init(Pcal6408A *self, I2cBus *i2c);
pcal6408a_gpio_ret_t pcal6408a_gpio_free(Pcal6408A *self);

