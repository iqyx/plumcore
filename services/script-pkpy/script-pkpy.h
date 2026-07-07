/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * pocketpy Python interpreter PoC service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"


typedef enum {
	SCRIPT_PKPY_RET_OK = 0,
	SCRIPT_PKPY_RET_FAILED,
	SCRIPT_PKPY_RET_NULL,
} script_pkpy_ret_t;


typedef struct {
	TaskHandle_t task;
} ScriptPkpy;


script_pkpy_ret_t script_pkpy_init(ScriptPkpy *self);
script_pkpy_ret_t script_pkpy_free(ScriptPkpy *self);
