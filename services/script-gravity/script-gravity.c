/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Gravity language PoC
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

#include "script-gravity.h"
#include "gravity_compiler.h"
#include "gravity_macros.h"
#include "gravity_core.h"
#include "gravity_vm.h"

#define MODULE_NAME "script-gravity"

static const char *script = "1 + 2";


static void script_gravity_error_handler(gravity_vm *vm, error_type_t error_type, const char *description, error_desc_t error_desc, void *xdata) {
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%s"), description);
}


static void script_gravity_task(void *p) {
	ScriptGravity *self = (ScriptGravity *)p;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("compiling Gravity source"));
	gravity_delegate_t delegate = {.error_callback = script_gravity_error_handler};
	gravity_compiler_t *compiler = gravity_compiler_create(&delegate);
	gravity_closure_t *closure = gravity_compiler_run(compiler, script, strlen(script), 0, true, true);

	if (closure) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("running VM"));
		gravity_vm *vm = gravity_vm_new(&delegate);
		gravity_compiler_transfer(compiler, vm);
		if (gravity_vm_runmain(vm, closure)) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("VM finished"));
			// print result (INT) 30 in this simple example
			gravity_value_t result = gravity_vm_result(vm);
			char buf[64] = {0};
			gravity_value_dump(vm, result, buf, sizeof(buf));
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("result: %s"), buf);
		}
	}

	while (true) {
		vTaskDelay(1000);
	}

	gravity_compiler_free(compiler);
	gravity_vm_free(vm);
	gravity_core_free();

	/* Keep the task to be able to check the stack size used. */
	while (true) {
		vTaskDelay(1000);
	}

	vTaskDelete(NULL);
}


script_gravity_ret_t script_gravity_init(ScriptGravity *self) {
	memset(self, 0, sizeof(ScriptGravity));

	xTaskCreate(script_gravity_task, "script-gravity", configMINIMAL_STACK_SIZE + 1000, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return SCRIPT_GRAVITY_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module started"));

	return SCRIPT_GRAVITY_RET_OK;
}


script_gravity_ret_t script_gravity_free(ScriptGravity *self) {
	return SCRIPT_GRAVITY_RET_OK;
}
