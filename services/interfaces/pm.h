/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Power manager interface
 *
 * Copyright (c) 2023-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

typedef enum pm_ret {
	PM_RET_OK = 0,
	PM_RET_FAILED,
	PM_RET_NULL,
} pm_ret_t;

enum pm_state {
	/**
	 * @brief MCU sleep states.
	 *
	 * THe original intention there was to conform to ACPI states. However, they are not specifically suited
	 * for MCUs as a MCU combines CPU(s) and multiple buses and peripherals.
	 * The power manager instance may decide which power states it implements depending on the device
	 * and its purpose.
	 */

	/* Reserved value used as a list terminator and to denote "no state". It is never a valid
	 * power state, hence the states below count from 1. */
	PM_STATE_NONE = 0,

	/* All peripherals are clocked, core is executing instructions. */
	PM_STATE_S0 = 1,
	PM_STATE_RUN = 1,

	/* All peripherals are available, core is sleeping. Instant wake-up. */
	PM_STATE_S1 = 2,
	PM_STATE_SLEEP = 2,

	/* Stop modes/deep sleep modes. RAM is preserved, longer wake-up time, lower power consumption */
	PM_STATE_S2 = 3,
	PM_STATE_STOP0 = 3,
	PM_STATE_S3 = 4,
	PM_STATE_STOP1 = 4,
	PM_STATE_S4 = 5,
	PM_STATE_STOP2 = 5,

	/* Standby mode means the core is off, RAM contents is not preserved, RTC and backup domain is running,
	 * wake-up possible. */
	PM_STATE_S5 = 6,
	PM_STATE_STANDBY = 6,

	/* Power off mode. Wake-up possible by a few wakup lines. */
	PM_STATE_S6 = 7,
	PM_STATE_SHUTDOWN = 7,

	/**
	 * @brief Device power states
	 */
	PM_STATE_D0 = 10,
	PM_STATE_D1 = 11,
	PM_STATE_D2 = 12,
	PM_STATE_D3 = 13,
};

/*
 * One entry of the power-state-to-name mapping. Plain struct: its members are accessed directly by whichever
 * consumer walks the table.
 */
struct pm_state_name {
	enum pm_state state;
	const char *name;
};

/*
 * Mapping of power states to human-readable names, provided as an array-initializer literal rather than a
 * variable so this header stays declaration-only -- it defines no object and emits no data. A consumer
 * materializes it into its own storage, e.g.:
 *
 *     static const struct pm_state_name pm_state_names[] = PM_STATE_NAMES;
 *
 * The list is terminated by a PM_STATE_NONE / NULL entry. Each enum value appears once; the ACPI aliases
 * (PM_STATE_S0 etc.) share the device states' values and are intentionally omitted.
 */
#define PM_STATE_NAMES { \
	{ PM_STATE_RUN,      "run" }, \
	{ PM_STATE_SLEEP,    "sleep" }, \
	{ PM_STATE_STOP0,    "stop0" }, \
	{ PM_STATE_STOP1,    "stop1" }, \
	{ PM_STATE_STOP2,    "stop2" }, \
	{ PM_STATE_STANDBY,  "standby" }, \
	{ PM_STATE_SHUTDOWN, "shutdown" }, \
	{ PM_STATE_D0,       "D0" }, \
	{ PM_STATE_D1,       "D1" }, \
	{ PM_STATE_D2,       "D2" }, \
	{ PM_STATE_D3,       "D3" }, \
	{ PM_STATE_NONE,     NULL }, \
}

/*
 * Opaque lock handle returned by pm_lock_state() and passed back to pm_release_state(). It is
 * self-describing: it carries the state it holds and the bit it occupies in that state's lock
 * bitmap, so release needs nothing else to find and clear it.
 */
typedef struct pm_lock {
	enum pm_state state;
	uint32_t mask;
} PmLock;

typedef struct pm Pm;
struct pm_vmt {
	pm_ret_t (*pm_set_state)(Pm *self, enum pm_state state);
	/* Report the state the manager is currently in through @p state. */
	pm_ret_t (*pm_get_state)(Pm *self, enum pm_state *state);
	pm_ret_t (*pm_lock_state)(Pm *self, enum pm_state state, PmLock *lock);
	pm_ret_t (*pm_release_state)(Pm *self, PmLock *lock);
};

typedef struct pm {
	const struct pm_vmt *vmt;
	void *parent;
} Pm;

