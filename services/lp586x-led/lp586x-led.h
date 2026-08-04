/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments LP586x LED matrix driver
 *
 * References:
 *   - LP5862 datasheet SNVSC53: https://www.ti.com/lit/ds/symlink/lp5862.pdf
 *   - LP5860 datasheet SNVSC10: https://www.ti.com/lit/ds/symlink/lp5860.pdf
 *   - LP5860 register map SNVU786 (detailed register bit fields)
 *   - lp586x-rs reference driver (register layout cross-check): https://github.com/markus-k/lp586x-rs
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
	LP586X_RET_OK = 0,
	LP586X_RET_FAILED,
} lp586x_ret_t;

/**
 * @brief Exact part number of the driven device
 *
 * All LP586x parts drive their LEDs through a time-multiplexing matrix of 18 constant current sinks
 * (CS0..CS17) and a configurable number of scan lines. The parts only differ in the number of scan
 * lines, hence in the total LED dot count (18 dots per scan line):
 *
 *   LP5861: 1 line,   18 dots
 *   LP5862: 2 lines,  36 dots
 *   LP5864: 4 lines,  72 dots
 *   LP5866: 6 lines, 108 dots
 *   LP5868: 8 lines, 144 dots
 *   LP5860: 11 lines, 198 dots
 */
typedef enum {
	LP586X_TYPE_LP5861,
	LP586X_TYPE_LP5862,
	LP586X_TYPE_LP5864,
	LP586X_TYPE_LP5866,
	LP586X_TYPE_LP5868,
	LP586X_TYPE_LP5860,
} lp586x_type_t;

/* Service configuration passed to lp586x_init(). Not typedef'd per project policy. */
struct lp586x_conf {
	/** I2C bus the device is attached to. */
	I2cBus *i2c;
	/**
	 * 5-bit chip address of the device. The three most significant bits are fixed (100b), the two
	 * least significant bits are selected by the ADDR1/ADDR0 pins, giving 0x10 (both low) through
	 * 0x13. The two most significant register address bits are appended to it to form the
	 * transmitted I2C address.
	 */
	uint8_t addr;
	/** Exact part number, selecting the number of scan lines and thus the LED dot count. */
	lp586x_type_t type;
};

typedef struct lp586x {
	struct lp586x_conf conf;

	/* Array of per-dot PWM interfaces, dynamically allocated to match the LED dot count of the
	 * configured device type. */
	Pwm *channel;
	size_t channel_count;
} Lp586x;


/**
 * @brief Initialise the LP586x LED matrix driver
 *
 * The device is configured for data refresh mode 1 (8-bit PWM, updated instantly without a VSYNC
 * command), so writing a dot's PWM register takes effect immediately. Each dot is left at full analog
 * (DC) current and zero PWM, so the per-dot PWM duty alone controls the brightness afterwards.
 *
 * @param self Driver instance
 * @param conf Service configuration, copied into the instance
 */
lp586x_ret_t lp586x_init(Lp586x *self, const struct lp586x_conf *conf);
lp586x_ret_t lp586x_free(Lp586x *self);

/**
 * @brief Get the PWM interface of a single LED dot
 *
 * The LED dots are indexed in scan order, i.e. dot = scan_line * 18 + current_sink.
 *
 * @param self Driver instance
 * @param channel LED dot index (0 based)
 * @param pwm Output pointer receiving the dot's PWM interface
 */
lp586x_ret_t lp586x_get_pwm(Lp586x *self, size_t channel, Pwm **pwm);

/**
 * @brief Set the maximum analog (DC) current of a single LED dot
 *
 * As the device dims the LEDs with PWM, this sets the analog current driven at a 100% PWM duty, i.e.
 * the per-dot current the PWM duty scales down from. The value is a fraction (0.0 to 1.0) of the
 * per-dot full-scale current (the device-global maximum current times the colour-group current gain,
 * both left at their defaults).
 *
 * @param self Driver instance
 * @param channel LED dot index (0 based)
 * @param max Maximum current as a fraction (0.0 to 1.0) of the full-scale range
 */
lp586x_ret_t lp586x_set_max_current(Lp586x *self, size_t channel, float max);
