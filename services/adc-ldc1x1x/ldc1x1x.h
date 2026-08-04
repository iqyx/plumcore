/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * LDC1312/LDC1314 (12-bit) and LDC1612/LDC1614 (28-bit) Inductance to Digital Converter driver
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <stdbool.h>

#include <interfaces/sensor.h>
#include <interfaces/i2c-bus.h>


typedef enum  {
	LDC1X1X_RET_OK = 0,
	LDC1X1X_RET_FAILED,
	LDC1X1X_RET_NULL,
} ldc1x1x_ret_t;


/* Which of the two register-compatible families was detected on the bus. They differ in the data
 * register layout: the 12-bit parts keep one register per channel, the 28-bit parts use an MSB/LSB pair. */
enum ldc1x1x_part {
	LDC1X1X_PART_UNKNOWN = 0,
	LDC1X1X_PART_12BIT,       /* LDC1312 / LDC1314 */
	LDC1X1X_PART_28BIT,       /* LDC1612 / LDC1614 */
};


/* Service configuration passed to ldc1x1x_init(). Not typedef'd per project policy. The caller is
 * expected to fill every field; register parameters left at 0 fall back to a sensible driver default
 * (a raw 0 is not a useful value for any of them). */
struct ldc1x1x_conf {
	/** I2C bus the device is connected to. */
	I2cBus *i2c;
	/** 7-bit I2C address (e.g. 0x2a with the ADDR pin low, 0x2b with it high). */
	uint8_t addr;

	/** Command sent to @p preselect_cmd_addr before every device access, e.g. to switch an external
	 *  analog mux. Set @p preselect_cmd to NULL when no preselection is needed. */
	uint8_t *preselect_cmd;
	size_t preselect_cmd_len;
	uint8_t preselect_cmd_addr;

	/** Per-channel reference count (RCOUNT_CHx), sets the conversion time and thus the resolution. */
	uint16_t rcount[4];
	/** Per-channel settle count (SETTLECOUNT_CHx), the sensor settling time before each conversion. */
	uint16_t settlecount[4];
	/** Per-channel clock dividers (CLOCK_DIVIDERS_CHx): FIN_DIVIDER[15:12] (>= 1), FREF_DIVIDER[9:0]. */
	uint16_t clock_dividers[4];
	/** Per-channel sensor drive current (DRIVE_CURRENT_CHx): IDRIVE[15:11]. */
	uint16_t drive_current[4];

	/** Channel sequencing and input deglitch (MUX_CONFIG). */
	uint16_t mux_config;
	/** Device configuration (CONFIG), written last to bring the device out of sleep. */
	uint16_t config;
};


typedef struct ldc1x1x {
	struct ldc1x1x_conf conf;

	enum ldc1x1x_part part;

	Sensor out[4];

	/* Per-channel runtime enable. A channel is disabled after a persistent amplitude/watchdog error and
	 * then skipped by value reads until the device is re-enabled with ldc1x1x_enable(). */
	bool enabled[4];

	SemaphoreHandle_t select_lock;
} Ldc1x1x;


ldc1x1x_ret_t ldc1x1x_init(Ldc1x1x *self, const struct ldc1x1x_conf *conf);
ldc1x1x_ret_t ldc1x1x_enable(Ldc1x1x *self);

