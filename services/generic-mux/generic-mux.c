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
#include <interfaces/gpio.h>
#include <interfaces/mux.h>

#include "generic-mux.h"

#define MODULE_NAME "generic-mux"


/***************************************************************************************************
 * MUX interface API
 ***************************************************************************************************/

static mux_ret_t generic_mux_enable(Mux *self, bool enable) {
	GenericMux *mux = (GenericMux *)self->parent;

	if (mux->en_gpio != NULL) {
		mux->en_gpio->vmt->set(mux->en_gpio, enable);
	}
	return MUX_RET_OK;
}


static mux_ret_t generic_mux_select(Mux *self, uint32_t channel) {
	GenericMux *mux = (GenericMux *)self->parent;

	for (uint32_t i = 0; i < mux->line_count; i++) {
		Gpio *line = (*mux->lines)[i].gpio;
		line->vmt->set(line, (channel & (1 << i)) != 0);
	}

	return MUX_RET_OK;
}


static mux_ret_t generic_mux_channels(Mux *self, uint32_t *channels) {
	GenericMux *mux = (GenericMux *)self->parent;

	if (channels == NULL) {
		return MUX_RET_FAILED;
	}

	/* Each select line contributes one address bit, so the mux steers 2^line_count channels. */
	*channels = (uint32_t)1 << mux->line_count;
	return MUX_RET_OK;
}


static const struct mux_vmt generic_mux_vmt = {
	.enable = generic_mux_enable,
	.select = generic_mux_select,
	.channels = generic_mux_channels,
};


generic_mux_ret_t generic_mux_init(GenericMux *self, Gpio *en_gpio, const struct generic_mux_sel_line (*lines)[], uint32_t line_count) {
	memset(self, 0, sizeof(GenericMux));

	self->en_gpio = en_gpio;
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
