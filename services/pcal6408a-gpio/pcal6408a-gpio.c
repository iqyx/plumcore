/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Driver for GPIO over PCAL6408A expander
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <main.h>
#include <interfaces/i2c-bus.h>

#include "pcal6408a-gpio.h"

#define MODULE_NAME "pcal6408a-gpio"


static pcal6408a_gpio_ret_t pcal6408a_gpio_read8(Pcal6408A *self, uint8_t reg, uint8_t *val) {
	if (self->i2c->vmt->transfer(self->i2c, 0x20, &reg, sizeof(reg), val, sizeof(uint8_t)) != I2C_BUS_RET_OK) {
		return PCAL6408A_GPIO_RET_FAILED;
	}

	return PCAL6408A_GPIO_RET_OK;
}


static pcal6408a_gpio_ret_t pcal6408a_gpio_write8(Pcal6408A *self, uint8_t reg, uint8_t val) {
	uint8_t txdata[2] = {reg, val};
	if (self->i2c->vmt->transfer(self->i2c, 0x20, txdata, sizeof(txdata), NULL, 0) != I2C_BUS_RET_OK) {
		return PCAL6408A_GPIO_RET_FAILED;
	}

	return PCAL6408A_GPIO_RET_OK;
}


/***************************************************************************************************
 * GPIO interface API
 ***************************************************************************************************/

static gpio_ret_t gpio_set(Gpio *gpio, bool state) {
	Pcal6408A *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	if (state) {
		self->reg_opr |= (1 << pin);
	} else {
		self->reg_opr &= ~(1 << pin);
	}
	if (pcal6408a_gpio_write8(self, 0x01, self->reg_opr) != PCAL6408A_GPIO_RET_OK) {
		return GPIO_RET_FAILED;
	}

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_get(Gpio *gpio, bool *state) {
	Pcal6408A *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	if (pcal6408a_gpio_read8(self, 0x00, &self->reg_ipr) != PCAL6408A_GPIO_RET_OK) {
		return GPIO_RET_FAILED;
	}
	if (state == NULL) {
		return GPIO_RET_FAILED;
	}
	*state = self->reg_ipr & (1 << pin);

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_toggle(Gpio *gpio) {
	Pcal6408A *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	self->reg_opr ^= (1 << pin);
	if (pcal6408a_gpio_write8(self, 0x01, self->reg_opr) != PCAL6408A_GPIO_RET_OK) {
		return GPIO_RET_FAILED;
	}

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_set_mode(Gpio *gpio, enum gpio_mode mode) {
	Pcal6408A *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	if (mode == MODE_INPUT) {
		self->reg_cr |= (1 << pin);
	} else if (mode == MODE_OUTPUT) {
		self->reg_cr &= ~(1 << pin);
	}
	if (pcal6408a_gpio_write8(self, 0x03, self->reg_cr) != PCAL6408A_GPIO_RET_OK) {
		return GPIO_RET_FAILED;
	}

	return GPIO_RET_OK;
}


static gpio_ret_t gpio_set_pinmux(Gpio *gpio, uint32_t mux) {
	Pcal6408A *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	return GPIO_RET_FAILED;
}


static gpio_ret_t gpio_set_pull(Gpio *gpio, enum gpio_pull pull) {
	Pcal6408A *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	return GPIO_RET_FAILED;
}


static gpio_ret_t gpio_set_otype(Gpio *gpio, enum gpio_otype otype) {
	Pcal6408A *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	return GPIO_RET_FAILED;
}


static gpio_ret_t gpio_set_ospeed(Gpio *gpio, enum gpio_ospeed ospeed) {
	Pcal6408A *self = gpio->parent;
	int pin = gpio - &(self->pin[0]);

	return GPIO_RET_FAILED;
}


static const struct gpio_vmt pcal6408a_gpio_vmt = {
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

pcal6408a_gpio_ret_t pcal6408a_gpio_init(Pcal6408A *self, I2cBus *i2c) {
	memset(self, 0, sizeof(Pcal6408A));

	self->i2c = i2c;

	/* Match the expander reset values. */
	self->reg_opr = 0xff;
	self->reg_cr = 0xff;

	uint8_t val1;
	uint8_t val2;
	if (pcal6408a_gpio_read8(self, 0x42, &val1) != PCAL6408A_GPIO_RET_OK ||
	    pcal6408a_gpio_read8(self, 0x43, &val2) != PCAL6408A_GPIO_RET_OK) {
		goto err;
	}

	for (int i = 0; i < 8; i++) {
		self->pin[i].parent = self;
		self->pin[i].vmt = &pcal6408a_gpio_vmt;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return PCAL6408A_GPIO_RET_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("init/probe failed"));
	return PCAL6408A_GPIO_RET_OK;

}


pcal6408a_gpio_ret_t pcal6408a_gpio_free(Pcal6408A *self) {
	(void)self;
	return PCAL6408A_GPIO_RET_OK;
}



