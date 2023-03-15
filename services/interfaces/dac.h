/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Generic DAC interface
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

typedef enum {
	DAC_RET_OK,
	DAC_RET_FAILED,
} dac_ret_t;

typedef struct dac Dac;
typedef uint32_t dac_channel_t;
typedef float dac_sample_t;

struct dac_vmt {
	dac_ret_t (*set_single)(Dac *self, dac_sample_t sample);
};

typedef struct dac {
	const struct dac_vmt *vmt;
	void *parent;
} Dac;


