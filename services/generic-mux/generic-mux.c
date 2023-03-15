/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A generic analog/digital MUX configurable with GPIO
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/mux.h>
#include <libopencm3/stm32/gpio.h>

#include "generic-mux.h"

#define MODULE_NAME "generic-mux"


/***************************************************************************************************
 * MUX interface API
 ***************************************************************************************************/

static mux_ret_t generic_mux_enable(Mux *self, bool enable) {
	GenericMux *mux = (GenericMux *)self->parent;

	if (enable) {
		gpio_set(mux->en_port, mux->en_pin);
	} else {
		gpio_clear(mux->en_port, mux->en_pin);
	}
	return MUX_RET_OK;
}


static mux_ret_t generic_mux_select(Mux *self, uint32_t channel) {
	GenericMux *mux = (GenericMux *)self->parent;

	for (uint32_t i = 0; i < mux->line_count; i++) {
		if (channel & (1 << i)) {
			gpio_set((*mux->lines)[i].port, (*mux->lines)[i].pin);
		} else {
			gpio_clear((*mux->lines)[i].port, (*mux->lines)[i].pin);
		}
	}

	return MUX_RET_OK;
}


static const struct mux_vmt generic_mux_vmt = {
	.enable = generic_mux_enable,
	.select = generic_mux_select,
};


generic_mux_ret_t generic_mux_init(GenericMux *self, uint32_t en_port, uint32_t en_pin, const struct generic_mux_sel_line (*lines)[], uint32_t line_count) {
	memset(self, 0, sizeof(GenericMux));

	self->en_port = en_port;
	self->en_pin = en_pin;
	self->lines = lines;
	self->line_count = line_count;

	self->mux.parent = self;
	self->mux.vmt = &generic_mux_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialised with %d select lines"), line_count);
	return GENERIC_MUX_RET_OK;
}


generic_mux_ret_t generic_mux_free(GenericMux *self) {
	(void)self;
	return GENERIC_MUX_RET_OK;
}


