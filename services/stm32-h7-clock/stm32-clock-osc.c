/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 oscillator clock node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include "stm32-clock-osc.h"

#define MODULE_NAME "stm32-clock-osc"


static clock_ret_t stm32_clock_osc_enable(Clock *clock) {
	Stm32ClockOsc *self = clock->parent;

	/* Start the oscillator only on the first enable; subsequent callers just
	 * increment the reference count and return.  The hardware is left running
	 * until the last disable drops refcnt back to zero. */
	if (self->refcnt == 0) {
		*self->config.reg_enable.reg |= self->config.reg_enable.mask;
		/* Poll the ready bit for up to 10 ms.  A crystal typically locks in 1-2 ms;
		 * 10 ms covers slow or high-capacitance loads.  Failure here almost
		 * always indicates a hardware fault (missing crystal, open trace). */
		for (uint32_t i = 0; i < 10; i++) {
			if (*self->config.reg_ready.reg & self->config.reg_ready.mask) {
				u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "clock ready"));
				break;
			}
			vTaskDelay(1);
		}
		if (!(*self->config.reg_ready.reg & self->config.reg_ready.mask)) {
			u_log(system_log, LOG_TYPE_ERROR, CLOCK_PREFIX(self, "clock not ready, hw issue?"));
			return CLOCK_RET_FAILED;
		}
	}
	self->refcnt++;
	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "enabled, refcnt = %lu"), self->refcnt);
	return CLOCK_RET_OK;
}


static clock_ret_t stm32_clock_osc_disable(Clock *clock) {
	Stm32ClockOsc *self = clock->parent;

	/* Guard against spurious disable calls that arrive without a matching enable (refcnt already 0).
	 * Silently ignore them rather than wrapping the counter. */
	if (self->refcnt > 0) {
		self->refcnt--;
		u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "disabled, refcnt = %lu"), self->refcnt);
	}
	/* Stop the oscillator only once the last user has released it.  The enable bit is cleared here;
	 * the hardware will deassert the ready bit on its own — no need to poll for it. */
	if (self->refcnt == 0) {
		*self->config.reg_enable.reg &= ~self->config.reg_enable.mask;
		u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "clock off"));
	}
	return CLOCK_RET_OK;
}

static clock_ret_t stm32_clock_osc_get_rate(Clock *clock, uint32_t *rate_hz) {
	Stm32ClockOsc *self = clock->parent;
	if (rate_hz == NULL) {
		return CLOCK_RET_FAILED;
	}
	*rate_hz = self->rate_hz;

	return CLOCK_RET_OK;
}

const struct clock_vmt stm32_clock_osc_vmt = {
	.enable = stm32_clock_osc_enable,
	.disable = stm32_clock_osc_disable,
	.get_rate = stm32_clock_osc_get_rate,
};

stm32_clock_ret_t stm32_clock_osc_init(Stm32ClockOsc *self, const struct stm32_clock_osc_config *config) {
	memset(self, 0, sizeof(Stm32ClockOsc));
	memcpy(&self->config, config, sizeof(struct stm32_clock_osc_config));
	self->rate_hz = config->fixed_rate_hz;
	self->clock.name = self->config.name;
	self->clock.parent = self;
	self->clock.vmt = &stm32_clock_osc_vmt;

	if (self->config.fixed_rate_hz) {
		u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "fixed rate clock, rate = %lu MHz"), self->rate_hz / 1000000ul);
	} else {
		u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "variable rate clock, rate = %lu MHz, min_rate = %lu MHz, max_rate = %lu MHz"),
			self->rate_hz / 1000000ul,
			self->config.min_rate_hz / 1000000ul,
			self->config.max_rate_hz / 1000000ul
		);

	}
	return STM32_CLOCK_RET_OK;
}
