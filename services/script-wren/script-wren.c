/* SPDX-License-Identifier: BSD-2-Clause
 *
 * wren-io PoC
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include "script-wren.h"
#include "wren.h"

#define MODULE_NAME "script-wren"


static void script_wren_stdout_handler(WrenVM *vm, const char *s) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s"), s);
}


static void script_wren_error_handler(WrenVM *vm, WrenErrorType errorType, const char *module, const int line, const char *msg) {
	switch (errorType) {
		case WREN_ERROR_COMPILE:
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%s line %d: %s"), module, line, msg);
			break;
		case WREN_ERROR_STACK_TRACE:
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%s line %d: in %s"), module, line, msg);
			break;
		case WREN_ERROR_RUNTIME:
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("runtime error: %s"), msg);
			break;
		default:
			break;
	}
	vTaskDelay(2000);
}


static void script_wren_task(void *p) {
	ScriptWren *self = (ScriptWren *)p;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initializing Wren interpreter"));

	WrenConfiguration config;
	wrenInitConfiguration(&config);

	config.writeFn = &script_wren_stdout_handler;
	config.errorFn = &script_wren_error_handler;

	self->vm = wrenNewVM(&config);

	if (self->vm != NULL) {
		const char *module = "main";
		const char *script = "System.print(\"I am running in a VM!\")";

		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("running the sample script"));
		WrenInterpretResult result = wrenInterpret(self->vm, module, script);

		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("result %d"), result);

	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create Wren VM"));
	}
	/* Keep the task to be able to check the stack size used. */
	while (true) {
		vTaskDelay(1000);
	}

	wrenFreeVM(self->vm);
	vTaskDelete(NULL);
}


script_wren_ret_t script_wren_init(ScriptWren *self) {
	memset(self, 0, sizeof(ScriptWren));

	xTaskCreate(script_wren_task, "script-wren", configMINIMAL_STACK_SIZE + 2400, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return SCRIPT_WREN_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module started"));

	return SCRIPT_WREN_RET_OK;
}


script_wren_ret_t script_wren_free(ScriptWren *self) {
	if (u_assert(self != NULL)) {
		return SCRIPT_WREN_RET_NULL;
	}

	return SCRIPT_WREN_RET_OK;
}
