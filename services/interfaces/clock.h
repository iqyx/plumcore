/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic Clock interface
 *
 * Copyright (c) 2018-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>


typedef enum {
	CLOCK_RET_OK = 0,
	CLOCK_RET_FAILED,
} clock_ret_t;


typedef struct clock Clock;

struct clock_vmt {
	clock_ret_t (*set)(Clock *self, const struct timespec *time);
	clock_ret_t (*get)(Clock *self, struct timespec *time);

	/**
	 * Shift clock once by a configurable amount of nanoseconds
	 *
	 * @param time_ns Number of nanoseconds to shift. Set to 0 to disable. Negative values shift clock back.
	 */
	clock_ret_t (*shift)(Clock *self, int32_t time_ns);

	/**
	 * Fine adjust clock by skipping/inserting clock cycles periodically
	 *
	 * @param adjust_ppb Relative shift in ppb (number of clock cycles to skip/insert
	 *                   per billion of clock cycles at a nominal rate.
	 */
	clock_ret_t (*adjust)(Clock *self, int32_t adjust_ppb);

};


typedef struct clock {
	const struct clock_vmt *vmt;
	void *parent;
} Clock;


