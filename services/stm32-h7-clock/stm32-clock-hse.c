/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 HSE clock node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>
#include <stm32h7xx.h>

#include "stm32-clock-hse.h"

#define MODULE_NAME "stm32-clock-hse"


static clock_ret_t stm32_clock_hse_prepare(Clock *clock) {
	Stm32ClockHse *self = clock->parent;
	RCC_TypeDef *rcc = RCC;

	if (rcc->CR & RCC_CR_HSEON) {
		return CLOCK_RET_OK;
	}
	rcc->CR |= RCC_CR_HSEON;
	for (uint32_t i = 0; i < 10; i++) {
		if (rcc->CR & RCC_CR_HSERDY) {
			u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "clock ready"));
			return CLOCK_RET_OK;
		}
		vTaskDelay(1);
	}
	u_log(system_log, LOG_TYPE_ERROR, CLOCK_PREFIX(self, "clock not ready, hw issue?"));

	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_hse_unprepare(Clock *clock) {
	Stm32ClockHse *self = clock->parent;
	RCC_TypeDef *rcc = RCC;

	/* Clock not ready, nothing to unprepare. Fail early. */
	if (!(rcc->CR & RCC_CR_HSERDY)) {
		return CLOCK_RET_OK;
	}
	rcc->CR &= ~RCC_CR_HSEON;
	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "clock off"));

	return CLOCK_RET_OK;
}


static clock_ret_t stm32_clock_hse_enable(Clock *clock) {
	Stm32ClockHse *self = clock->parent;
	/* HSE is always gated on when prepared. It is a pure clock source, nothing to enable upstream.
	 * Just increase reference count to know if it is used or not. */
	self->refcnt++;
	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "enabled, refcnt = %lu"), self->refcnt);
	return CLOCK_RET_OK;
}


static clock_ret_t stm32_clock_hse_disable(Clock *clock) {
	Stm32ClockHse *self = clock->parent;
	/* Do not disable anything, just decrease the reference counter. */
	if (self->refcnt > 0) {
		self->refcnt--;
		u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "disabled, refcnt = %lu"), self->refcnt);
	}
	return CLOCK_RET_OK;
}

static clock_ret_t stm32_clock_hse_get_rate(Clock *clock, uint32_t *rate_hz) {
	Stm32ClockHse *self = clock->parent;
	if (rate_hz == NULL) {
		return CLOCK_RET_FAILED;
	}
	*rate_hz = self->rate_hz;

	return CLOCK_RET_OK;
}

const struct clock_vmt stm32_clock_hse_vmt = {
	.prepare = stm32_clock_hse_prepare,
	.unprepare = stm32_clock_hse_unprepare,
	.enable = stm32_clock_hse_enable,
	.disable = stm32_clock_hse_disable,
	.get_rate = stm32_clock_hse_get_rate,
};

stm32_clock_ret_t stm32_clock_hse_init(Stm32ClockHse *self, uint32_t rate_hz, bool bypass) {
	memset(self, 0, sizeof(Stm32ClockHse));
	self->rate_hz = rate_hz;
	self->clock.name = "hse-ck";
	self->clock.parent = self;
	self->clock.vmt = &stm32_clock_hse_vmt;

	RCC_TypeDef *rcc = RCC;

	/* Return the clock to the known state when the clock node starts. */
	if (rcc->CR & RCC_CR_HSEON) {
		u_log(system_log, LOG_TYPE_WARN, CLOCK_PREFIX(self, "HSE clock is already on, disabling"));
		rcc->CR &= ~RCC_CR_HSEON;
	}
	if (bypass) {
		rcc->CR |= RCC_CR_HSEBYP;
	}

	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "rate %lu MHz"), self->rate_hz / 1000000);
	return STM32_CLOCK_RET_OK;
}
