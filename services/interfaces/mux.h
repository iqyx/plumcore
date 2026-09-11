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

	/**
	 * @brief Get the number of selectable channels
	 *
	 * @param self Mux interface instance
	 * @param channels Set to the number of channels the mux can select. May not be NULL.
	 * @return MUX_RET_FAILED on error or MUX_RET_OK otherwise.
	 */
	mux_ret_t (*channels)(Mux *self, uint32_t *channels);
};

typedef struct mux {
	const struct mux_vmt *vmt;
	void *parent;
} Mux;
