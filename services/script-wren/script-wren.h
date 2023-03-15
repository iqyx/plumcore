/* SPDX-License-Identifier: BSD-2-Clause
 *
 * wren-io PoC
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "wren.h"


typedef enum {
	SCRIPT_WREN_RET_OK = 0,
	SCRIPT_WREN_RET_FAILED,
	SCRIPT_WREN_RET_NULL,
} script_wren_ret_t;


typedef struct {
	TaskHandle_t task;
	WrenVM *vm;

} ScriptWren;


script_wren_ret_t script_wren_init(ScriptWren *self);
script_wren_ret_t script_wren_free(ScriptWren *self);
