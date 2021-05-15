/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Flash volumes service (static configuration)
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

#include <interfaces/flash.h>


#define FLASH_VOL_STATIC_LVS_MAX 8

typedef enum {
	FLASH_VOL_STATIC_RET_OK = 0,
	FLASH_VOL_STATIC_RET_FAILED,
} flash_vol_static_ret_t;

typedef struct flash_vol_static FlashVolStatic;

struct flash_vol_lv {
	/* Must be first */
	Flash lv;
	FlashVolStatic *parent;
	size_t start;
	size_t size;
	const char *name;
};

typedef struct flash_vol_static {
	Flash *pv;
	struct flash_vol_lv lvs[FLASH_VOL_STATIC_LVS_MAX];


} FlashVolStatic;


flash_vol_static_ret_t flash_vol_static_init(FlashVolStatic *self, Flash *pv);
flash_vol_static_ret_t flash_vol_static_free(FlashVolStatic *self);
flash_vol_static_ret_t flash_vol_static_create(FlashVolStatic *self, const char *name, size_t start, size_t size, Flash **lv);
