/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 clock gate node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include "stm32-clock-gate.h"

#define MODULE_NAME "stm32-clock-gate"


static clock_ret_t stm32_clock_gate_enable(Clock *clock) {
	Stm32ClockGate *self = clock->parent;

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

	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "enabled"));
	self->refcnt++;
	return CLOCK_RET_OK;
err:
	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_gate_disable(Clock *clock) {
	Stm32ClockGate *self = clock->parent;

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


static clock_ret_t stm32_clock_gate_get_rate(Clock *clock, uint32_t *rate_hz) {
	Stm32ClockGate *self = clock->parent;

	if (!(self->config.parent && self->config.parent->vmt->get_rate)) {
		return CLOCK_RET_FAILED;
	}

	clock_ret_t ret = self->config.parent->vmt->get_rate(self->config.parent, rate_hz);
	if (ret == CLOCK_RET_OK) {
		u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "get_rate, rate_hz = %lu MHz"), *rate_hz / 1000000);
	}
	return ret;
}


const struct clock_vmt stm32_clock_gate_vmt = {
	.enable = stm32_clock_gate_enable,
	.disable = stm32_clock_gate_disable,
	.get_rate = stm32_clock_gate_get_rate,
};

stm32_clock_ret_t stm32_clock_gate_init(Stm32ClockGate *self, const struct stm32_clock_gate_config *config) {
	memset(self, 0, sizeof(Stm32ClockGate));
	memcpy(&self->config, config, sizeof(self->config));

	self->clock.parent = self;
	self->clock.name = config->name;
	self->clock.vmt = &stm32_clock_gate_vmt;

	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "init"));
	return STM32_CLOCK_RET_OK;
}
