/* SPDX-License-Identifier: BSD-2-Clause
 *
 * A generic whatever-MUX interface
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	MUX_RET_OK = 0,
	MUX_RET_FAILED,
} mux_ret_t;

typedef struct mux Mux;

struct mux_vmt {
	mux_ret_t (*enable)(Mux *self, bool enable);
	mux_ret_t (*select)(Mux *self, uint32_t channel);
};

typedef struct mux {
	const struct mux_vmt *vmt;
	void *parent;
} Mux;
