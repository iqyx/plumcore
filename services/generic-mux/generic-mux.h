/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A generic analog/digital MUX configurable with GPIO
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/mux.h>

typedef enum  {
	GENERIC_MUX_RET_OK = 0,
	GENERIC_MUX_RET_FAILED,
} generic_mux_ret_t;

struct generic_mux_sel_line {
	uint32_t port;
	uint32_t pin;
};

typedef struct generic_mux {
	/* libopencm3 GPIO style enable */
	uint32_t en_port;
	uint32_t en_pin;

	/* Select lines A0, A1, ... */
	const struct generic_mux_sel_line (*lines)[];
	uint32_t line_count;

	Mux mux;
} GenericMux;


generic_mux_ret_t generic_mux_init(GenericMux *self, uint32_t en_port, uint32_t en_pin, const struct generic_mux_sel_line (*lines)[], uint32_t line_count);
generic_mux_ret_t generic_mux_free(GenericMux *self);

