/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Power manager interface
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
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

	/* All peripherals are clocked, core is executing instructions. */
	PM_STATE_S0 = 0,
	PM_STATE_RUN = 0,

	/* All peripherals are available, core is sleeping. Instant wake-up. */
	PM_STATE_S1 = 1,
	PM_STATE_SLEEP = 1,

	/* Stop modes/deep sleep modes. RAM is preserved, longer wake-up time, lower power consumption */
	PM_STATE_S2 = 2,
	PM_STATE_STOP0 = 2,
	PM_STATE_S3 = 3,
	PM_STATE_STOP1 = 3,
	PM_STATE_S4 = 4,
	PM_STATE_STOP2 = 4,

	/* Standby mode means the core is off, RAM contents is not preserved, RTC and backup domain is running,
	 * wake-up possible. */
	PM_STATE_S5 = 5,
	PM_STATE_STANDBY = 5,

	/* Power off mode. Wake-up possible by a few wakup lines. */
	PM_STATE_S6 = 6,
	PM_STATE_SHUTDOWN = 6,
};

typedef struct wakelock {
	uint32_t lock;
} WakeLock;

typedef struct pm Pm;
struct pm_vmt {
	pm_ret_t (*acquire_wakelock)(Pm *self, WakeLock *wl, enum pm_state wl_state);
	pm_ret_t (*release_wakelock)(Pm *self, WakeLock *wl);
};

typedef struct pm {
	const struct pm_vmt *vmt;
	void *parent;
} Pm;



