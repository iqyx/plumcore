/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 generic clock divider node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include "stm32-clock-div.h"

#define MODULE_NAME "stm32-clock-div"


static clock_ret_t stm32_clock_div_enable(Clock *clock) {
	Stm32ClockDiv *self = clock->parent;

	if (self->refcnt) {
		self->refcnt++;
		return CLOCK_RET_OK;
	}

	if (!(self->config.parent && self->config.parent->vmt->enable &&
	    self->config.parent->vmt->enable(self->config.parent) == CLOCK_RET_OK)) {
		goto err;
	}

	uint32_t parent_rate = 0;
	if (!(self->config.parent && self->config.parent->vmt->get_rate &&
	    self->config.parent->vmt->get_rate(self->config.parent, &parent_rate) == CLOCK_RET_OK)) {
		goto err;
	}

	uint32_t prescaler = parent_rate / self->rate_hz;
	if (prescaler < 1 || prescaler > self->config.max_prescaler) {
		/* Prescaler out of bounds, do not change anything. */
		goto err;
	}

	if ((parent_rate / prescaler) != self->rate_hz) {
		/* Impossible to get the desired rate, treat as error.
		 * The desired rate should be checked by round_rate first. */
		goto err;
	}

	uint32_t reg = *(self->config.reg_pllp.reg);
	reg &= ~(self->config.reg_pllp.mask);
	reg |= (uint32_t)(prescaler - self->config.offset_pllp) << self->config.reg_pllp.shift;
	*(self->config.reg_pllp.reg) = reg;

	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "enabled with prescaler %u"), prescaler);

	self->refcnt++;
	return CLOCK_RET_OK;
err:
	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_div_disable(Clock *clock) {
	Stm32ClockDiv *self = clock->parent;

	if (self->refcnt == 0) {
		return CLOCK_RET_OK;
	}

	if (!(self->config.parent && self->config.parent->vmt->disable &&
	    self->config.parent->vmt->disable(self->config.parent) == CLOCK_RET_OK)) {
		goto err;
	}

	/* Write zero prescaler to gate the clock off. */
	uint32_t reg = *(self->config.reg_pllp.reg);
	reg &= ~(self->config.reg_pllp.mask);
	*(self->config.reg_pllp.reg) = reg;

	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "disabled"));

	self->refcnt--;
	return CLOCK_RET_OK;
err:
	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_div_get_rate(Clock *clock, uint32_t *rate_hz) {
	Stm32ClockDiv *self = clock->parent;

	uint32_t prescaler = (*self->config.reg_pllp.reg & self->config.reg_pllp.mask) >> self->config.reg_pllp.shift;
	if (prescaler == 0) {
		/* Divider is disabled (gated off). */
		if (rate_hz != NULL) {
			*rate_hz = self->rate_hz;
		}
		/* Do not change self->rate_hz here! */
		u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "get_rate, rate_hz = %lu MHz"), *rate_hz / 1000000);
		return CLOCK_RET_OK;
	}
	prescaler += self->config.offset_pllp;

	uint32_t parent_rate = 0;
	if (self->config.parent && self->config.parent->vmt->get_rate &&
	    self->config.parent->vmt->get_rate(self->config.parent, &parent_rate) == CLOCK_RET_OK) {
		if (rate_hz != NULL) {
			self->rate_hz = parent_rate / prescaler;
			*rate_hz = self->rate_hz;

		}
		return CLOCK_RET_OK;
	}

	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_div_set_rate(Clock *clock, uint32_t rate_hz) {
	Stm32ClockDiv *self = clock->parent;

	if (rate_hz < self->config.min_rate_hz || rate_hz > self->config.max_rate_hz) {
		return CLOCK_RET_FAILED;
	}

	uint32_t prescaler = (*self->config.reg_pllp.reg & self->config.reg_pllp.mask) >> self->config.reg_pllp.shift;
	if (prescaler == 0) {
		/* Divider is disabled, just save the desired rate value. */
		self->rate_hz = rate_hz;
	} else {
		/* Divider is enabled. Disable first, reconfigure, enable. */
		if (!(self->config.parent && self->config.parent->vmt->disable &&
		    self->config.parent->vmt->disable(self->config.parent) != CLOCK_RET_OK)) {
			goto err;
		}

		self->rate_hz = rate_hz;

		if (!(self->config.parent && self->config.parent->vmt->enable &&
		    self->config.parent->vmt->enable(self->config.parent) != CLOCK_RET_OK)) {
			goto err;
		}
	}
	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "set rate to %lu Hz"), self->rate_hz);
	return CLOCK_RET_OK;
err:
	return CLOCK_RET_FAILED;
}


const struct clock_vmt stm32_clock_div_vmt = {
	.enable = stm32_clock_div_enable,
	.disable = stm32_clock_div_disable,
	.get_rate = stm32_clock_div_get_rate,
	.set_rate = stm32_clock_div_set_rate,
};

stm32_clock_ret_t stm32_clock_div_init(Stm32ClockDiv *self, const struct stm32_clock_div_config *config) {
	memset(self, 0, sizeof(Stm32ClockDiv));
	memcpy(&self->config, config, sizeof(self->config));

	self->clock.parent = self;
	self->clock.name = config->name;
	self->clock.vmt = &stm32_clock_div_vmt;

	uint32_t reg = *(self->config.reg_pllp.reg);
	reg &= ~(self->config.reg_pllp.mask);
	*(self->config.reg_pllp.reg) = reg;

	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "init"));
	return STM32_CLOCK_RET_OK;
}
