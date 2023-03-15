/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Gravity language PoC
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "gravity_compiler.h"
#include "gravity_macros.h"
#include "gravity_core.h"
#include "gravity_vm.h"


typedef enum {
	SCRIPT_GRAVITY_RET_OK = 0,
	SCRIPT_GRAVITY_RET_FAILED,
	SCRIPT_GRAVITY_RET_NULL,
} script_gravity_ret_t;


typedef struct {
	TaskHandle_t task;

} ScriptGravity;


script_gravity_ret_t script_gravity_init(ScriptGravity *self);
script_gravity_ret_t script_gravity_free(ScriptGravity *self);
