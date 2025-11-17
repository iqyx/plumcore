/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GPIO interface
 *
 * Copyright (c) 2024-2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
	GPIO_RET_OK = 0,
	GPIO_RET_FAILED,
} gpio_ret_t;

enum gpio_mode {
	MODE_INPUT = 0,
	MODE_OUTPUT,
	MODE_ALTERNATE,
	MODE_ANALOG,
};

enum gpio_pull {
	PULL_NONE = 0,
	PULL_UP,
	PULL_DOWN,
};

enum gpio_otype {
	OTYPE_PP,
	OTYPE_OD,
};

enum gpio_ospeed {
	OSPEED_LOW = 0,
	OSPEED_MEDIUM,
	OSPEED_HIGH,
	OSPEED_VERYHIGH,
};

typedef struct gpio Gpio;

struct gpio_vmt {
	gpio_ret_t (*set)(Gpio *self, bool state);
	gpio_ret_t (*get)(Gpio *self, bool *state);
	gpio_ret_t (*toggle)(Gpio *self);
	gpio_ret_t (*set_mode)(Gpio *self, enum gpio_mode mode);
	gpio_ret_t (*set_pinmux)(Gpio *self, uint32_t mux);
	gpio_ret_t (*set_pull)(Gpio *self, enum gpio_pull pull);
	gpio_ret_t (*set_otype)(Gpio *self, enum gpio_otype otype);
	gpio_ret_t (*set_ospeed)(Gpio *self, enum gpio_ospeed ospeed);
};


typedef struct gpio {
	const struct gpio_vmt *vmt;
	void *parent;
} Gpio;

