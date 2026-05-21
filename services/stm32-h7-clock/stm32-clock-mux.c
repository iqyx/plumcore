/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 generic clock mux node
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>

#include "stm32-clock-mux.h"

#define MODULE_NAME "stm32-clock-mux"


static clock_ret_t stm32_clock_mux_enable(Clock *clock) {
	Stm32ClockMux *self = clock->parent;

	/* Already enabled. */
	if (self->refcnt) {
		self->refcnt++;
		u_log(system_log, LOG_TYPE_ERROR, CLOCK_PREFIX(self, "enabled, refcnt = %lu"), self->refcnt);
		return CLOCK_RET_OK;
	}

	if (self->current_parent == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, CLOCK_PREFIX(self, "invalid input clock"));
		goto err;
	}

	if (self->config.reg == NULL || self->config.mask == 0) {
		/* Mux not configured yet. Don't know how to manipulate the mux. */
		goto err;
	}

	/* Validate the current parent. Determine the index in the input source list. */
	for (size_t i = 0; i < self->config.input_count; i++) {
		if (self->config.inputs[i] == self->current_parent) {

			/* Enable the upstream clock and exit with success. */
			if (self->current_parent->vmt->enable &&
			    self->current_parent->vmt->enable(self->current_parent) == CLOCK_RET_OK) {
				self->refcnt++;
				u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "enabling, refcnt = %lu, reg = %lu, parent = %s"), self->refcnt, i, self->current_parent->name);

				uint32_t reg = *(self->config.reg);
				reg &= ~(self->config.mask);
				reg |= i << self->config.shift;
				*(self->config.reg) = reg;

				return CLOCK_RET_OK;
			}
		}
	}

err:
	/* No matching input found. Clock cannot be enabled. Do not increase refcount
	 * nor do not enable the upstream source. */
	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_mux_disable(Clock *clock) {
	Stm32ClockMux *self = clock->parent;
	if (self->refcnt == 0) {
		/* Cannot disable a disabled mux any more. */
		return CLOCK_RET_OK;
	} else {
		self->refcnt--;
	}

	if (self->refcnt == 0) {
		u_log(system_log, LOG_TYPE_ERROR, CLOCK_PREFIX(self, "disabling"));

		uint32_t reg = *(self->config.reg);
		reg &= ~(self->config.mask);
		reg |= self->config.disabled_id << self->config.shift;
		*(self->config.reg) = reg;

		/* Disable the upstream clock and exit with success. */
		if (self->current_parent->vmt->disable &&
		    self->current_parent->vmt->disable(self->current_parent) == CLOCK_RET_OK) {
			self->refcnt--;
			return CLOCK_RET_OK;
		}
	} else {
		u_log(system_log, LOG_TYPE_ERROR, CLOCK_PREFIX(self, "not disabling yet, refcnt = %lu"), self->refcnt);
		return CLOCK_RET_OK;
	}

	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_mux_set_parent(Clock *clock, Clock *parent) {
	Stm32ClockMux *self = clock->parent;

	if (parent == NULL) {
		/* Cannot set NULL clock. Considered an error state. */
		goto err;
	}

	if (self->config.reg == NULL || self->config.mask == 0) {
		/* Mux not configured yet. */
		goto err;
	}

	for (size_t i = 0; i < self->config.input_count; i++) {
		if (self->config.inputs[i] == parent) {
			if (self->refcnt) {
				u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "change parent while enabled, parent = %s"), parent->name);

				/* New clock source found. Disable the current clock input, set the new input,
				 * reenable the input clock. */
				if (!(self->current_parent && self->current_parent->vmt->disable &&
				    self->current_parent->vmt->disable(self->current_parent) != CLOCK_RET_OK)) {
					goto err;
				}
				self->current_parent = parent;
				if (!(self->current_parent->vmt->enable &&
				    self->current_parent->vmt->enable(self->current_parent) != CLOCK_RET_OK)) {
					goto err;
				}
			} else {
				u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "change parent, parent = %s"), parent->name);

				/* Currently the mux is disabled. Do not disable/enable the input clock. */
				self->current_parent = parent;
			}

			return CLOCK_RET_OK;
		}
	}

err:
	return CLOCK_RET_FAILED;
}


static clock_ret_t stm32_clock_mux_get_rate(Clock *clock, uint32_t *rate_hz) {
	Stm32ClockMux *self = clock->parent;
	if (rate_hz == NULL) {
		return CLOCK_RET_FAILED;
	}

	if (!(self->current_parent && self->current_parent->vmt->get_rate)) {
		return CLOCK_RET_FAILED;
	}

	clock_ret_t ret = self->current_parent->vmt->get_rate(self->current_parent, rate_hz);
	if (ret == CLOCK_RET_OK) {
		u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "get_rate, rate_hz = %lu MHz"), *rate_hz / 1000000);
	}
	return ret;
}


const struct clock_vmt stm32_clock_mux_vmt = {
	.enable = stm32_clock_mux_enable,
	.disable = stm32_clock_mux_disable,
	.set_parent = stm32_clock_mux_set_parent,
	.get_rate = stm32_clock_mux_get_rate,
};

stm32_clock_ret_t stm32_clock_mux_init(Stm32ClockMux *self, const struct stm32_clock_mux_config *config) {
	memset(self, 0, sizeof(Stm32ClockMux));
	memcpy(&self->config, config, sizeof(self->config));

	self->clock.parent = self;
	self->clock.name = config->name;
	self->clock.vmt = &stm32_clock_mux_vmt;

	u_log(system_log, LOG_TYPE_INFO, CLOCK_PREFIX(self, "init"));
	return STM32_CLOCK_RET_OK;
}
