/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 PLL multiplier node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include "stm32-clock-mul.h"

#define MODULE_NAME "stm32-clock-mul"


static clock_ret_t stm32_clock_mul_enable(Clock *clock) {
	Stm32ClockMul *self = clock->parent;

	if (self->refcnt) {
		self->refcnt++;
		return CLOCK_RET_OK;
	}

	if (!(self->config.parent && self->config.parent->vmt->enable &&
	    self->config.parent->vmt->enable(self->config.parent) == CLOCK_RET_OK)) {
		goto err;
	}

	uint32_t reg = *(self->config.reg_enable.reg);
	reg |= self->config.reg_enable.mask;
	*(self->config.reg_enable.reg) = reg;

	for (uint32_t i = 0; i < 10; i++) {
		if (*(self->config.reg_ready.reg) & self->config.reg_ready.mask) {
			u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "enabled"));
			self->refcnt++;
			return CLOCK_RET_OK;
		}
		vTaskDelay(1);
	}
	u_log(system_log, LOG_TYPE_ERROR, CLOCK_PREFIX(self, "clock not ready, hw issue?"));

err:
	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_mul_disable(Clock *clock) {
	Stm32ClockMul *self = clock->parent;

	if (self->refcnt == 0) {
		return CLOCK_RET_OK;
	}

	uint32_t reg = *(self->config.reg_enable.reg);
	reg &= ~self->config.reg_enable.mask;
	*(self->config.reg_enable.reg) = reg;

	if (!(self->config.parent && self->config.parent->vmt->disable &&
	    self->config.parent->vmt->disable(self->config.parent) == CLOCK_RET_OK)) {
		goto err;
	}

	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "disabled"));

	self->refcnt--;
	return CLOCK_RET_OK;
err:
	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_mul_get_rate(Clock *clock, uint32_t *rate_hz) {
	Stm32ClockMul *self = clock->parent;

	uint32_t multiplier = ((*self->config.reg_plln.reg & self->config.reg_plln.mask) >> self->config.reg_plln.shift) + self->config.offset_plln;
	if (multiplier < self->config.min_multiplier || multiplier > self->config.max_multiplier) {
		return CLOCK_RET_FAILED;
	}

	uint32_t parent_rate = 0;
	if (self->config.parent && self->config.parent->vmt->get_rate &&
	    self->config.parent->vmt->get_rate(self->config.parent, &parent_rate) == CLOCK_RET_OK) {
		if (rate_hz != NULL) {
			self->rate_hz = parent_rate * multiplier;
			*rate_hz = self->rate_hz;
			u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "get_rate, rate_hz = %lu MHz"), *rate_hz / 1000000);
		}
		return CLOCK_RET_OK;
	}

	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_mul_set_rate(Clock *clock, uint32_t rate_hz) {
	Stm32ClockMul *self = clock->parent;

	if (rate_hz < self->config.min_rate_hz || rate_hz > self->config.max_rate_hz) {
		return CLOCK_RET_FAILED;
	}

	/* Cannot reconfigure a running PLL. */
	if (*(self->config.reg_enable.reg) & self->config.reg_enable.mask) {
		goto err;
	}

	uint32_t parent_rate = 0;
	if (!(self->config.parent && self->config.parent->vmt->get_rate &&
	    self->config.parent->vmt->get_rate(self->config.parent, &parent_rate) == CLOCK_RET_OK)) {
		goto err;
	}

	uint32_t multiplier = rate_hz / parent_rate;
	if (multiplier < self->config.min_multiplier || multiplier > self->config.max_multiplier) {
		goto err;
	}

	uint32_t reg = *(self->config.reg_plln.reg);
	reg &= ~self->config.reg_plln.mask;
	reg |= (multiplier - self->config.offset_plln) << self->config.reg_plln.shift;
	*(self->config.reg_plln.reg) = reg;

	self->rate_hz = rate_hz;
	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "set rate to %lu Hz"), self->rate_hz);
	return CLOCK_RET_OK;
err:
	return CLOCK_RET_FAILED;
}


const struct clock_vmt stm32_clock_mul_vmt = {
	.enable = stm32_clock_mul_enable,
	.disable = stm32_clock_mul_disable,
	.get_rate = stm32_clock_mul_get_rate,
	.set_rate = stm32_clock_mul_set_rate,
};

stm32_clock_ret_t stm32_clock_mul_init(Stm32ClockMul *self, const struct stm32_clock_mul_config *config) {
	memset(self, 0, sizeof(Stm32ClockMul));
	memcpy(&self->config, config, sizeof(self->config));

	self->clock.parent = self;
	self->clock.name = config->name;
	self->clock.vmt = &stm32_clock_mul_vmt;

	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "init"));
	return STM32_CLOCK_RET_OK;
}
