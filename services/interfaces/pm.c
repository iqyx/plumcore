/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Power manager interface helpers
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Thanks jpa- for the atomic code.
 */

#include <stdint.h>
#include <stdatomic.h>
#include "pm.h"


pm_ret_t wakelock_acquire(WakeLockGroup *self, WakeLock *wl) {
	if (self->bitmask == UINT32_MAX) {
		/* Too many wakelocks acquired. */
		return PM_RET_FAILED;
	}
	uint32_t ex_wl;
	do {
		ex_wl = self->bitmask;
		wl->lock = 1 << (31 - __builtin_clz(~ex_wl));
	} while (!atomic_compare_exchange_strong(&self->bitmask, &ex_wl, ex_wl ^ wl->lock));
	return PM_RET_OK;
}


pm_ret_t wakelock_release(WakeLockGroup *self, WakeLock *wl) {
	if (wl->lock == 0) {
		/* Wakelock was not properly acquired. */
		return PM_RET_FAILED;
	}
	if (!(self->bitmask & wl->lock)) {
		/* Releasing a wakelock which was already released. */
		return PM_RET_FAILED;
	}
	atomic_fetch_and(&self->bitmask, ~wl->lock);
	wl->lock = 0;
	return PM_RET_OK;
}

