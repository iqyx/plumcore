#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "interface.h"


typedef enum {
	CLOCK_RET_OK = 0,
	CLOCK_RET_FAILED,
} clock_ret_t;

typedef struct {
	Interface interface;

	void *parent;

	clock_ret_t (*set)(void *parent, const struct timespec *time);
	clock_ret_t (*get)(void *parent, struct timespec *time);

	/* Clock disciplining functions */

	/**
	 * Shift clock periodically by a configurable amount of nanoseconds
	 *
	 * @param time_ns Number of nanoseconds to shift. Set to 0 to disable. Negative values shift clock back.
	 */
	clock_ret_t (*shift)(void *parent, int32_t time_ns);

	/**
	 * Fine adjust clock by skipping/inserting clock cycles
	 *
	 * @param adjust_ppb Relative shift in ppb (number of clock cycles to skip/insert
	 *                   per billion of clock cycles at a nominal rate.
	 */
	clock_ret_t (*adjust)(void *parent, int32_t adjust_ppb);
} Clock;


clock_ret_t clock_init(Clock *self);
clock_ret_t clock_free(Clock *self);
