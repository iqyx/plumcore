/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments LP5810/LP5812 LED driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/pwm.h>

typedef enum {
	LP581X_RET_OK = 0,
	LP581X_RET_FAILED,
} lp581x_ret_t;

/**
 * @brief Exact part number of the driven device
 *
 * The number of available output channels depends on the concrete part:
 * the LP5810 has 4 outputs, the LP5812 has 12.
 */
typedef enum {
	LP581X_TYPE_LP5810,
	LP581X_TYPE_LP5812,
} lp581x_type_t;

typedef struct lp581x {
	I2cBus *i2c;
	uint8_t addr;
	lp581x_type_t type;

	/* Array of per-output PWM interfaces, dynamically allocated to match the
	 * channel count of the configured device type. */
	Pwm *channel;
	size_t channel_count;

	/* Register and enable-bit offset of the first driven output. The 12-channel parts drive their
	 * LEDs through the scan matrix, whose registers start after the four direct-drive outputs. */
	uint8_t channel_base;
} Lp581x;


/**
 * @brief Initialise the LP581x LED driver
 *
 * @param self Driver instance
 * @param i2c I2C bus the device is attached to
 * @param addr 5-bit chip address of the device, selected by the part suffix (0x14 for the A
 *             variant, 0x15 for B, 0x16 for C, 0x17 for D). The two most significant register
 *             address bits are appended to it to form the transmitted I2C address.
 * @param type Exact part number, selecting the number of output channels
 */
lp581x_ret_t lp581x_init(Lp581x *self, I2cBus *i2c, uint8_t addr, lp581x_type_t type);
lp581x_ret_t lp581x_free(Lp581x *self);

/**
 * @brief Get the PWM interface of a single output channel
 *
 * @param self Driver instance
 * @param channel Output channel index (0 based)
 * @param pwm Output pointer receiving the channel's PWM interface
 */
lp581x_ret_t lp581x_get_pwm(Lp581x *self, size_t channel, Pwm **pwm);

/**
 * @brief Set the maximum analog (DC) current of a single output channel
 *
 * As the device dims the LEDs with PWM, this sets the analog current driven at a 100% PWM duty,
 * i.e. the per-channel current the PWM duty scales down from. The value is a fraction (0.0 to 1.0)
 * of the device full-scale current range.
 *
 * @param self Driver instance
 * @param channel Output channel index (0 based)
 * @param max Maximum current as a fraction (0.0 to 1.0) of the full-scale range
 */
lp581x_ret_t lp581x_set_max_current(Lp581x *self, size_t channel, float max);
