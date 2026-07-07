/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * pocketpy Python interpreter PoC service
 *
 * Runs a small Python script on the pocketpy VM and reports its footprint: free heap before
 * and after execution (RAM usage), the task stack high water mark (stack usage) and, indirectly,
 * the flash usage which can be read from the linker size output. This mirrors the other script-*
 * PoC services (script-es, script-wren, script-gravity) used to compare embeddable interpreters.
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
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

#include "script-pkpy.h"
#include "pocketpy/pocketpy.h"

#define MODULE_NAME "script-pkpy"

/* Small Python program executed on the VM. */
static const char *script = "print('hello from pocketpy'); 20 + 22";


/* pocketpy routes the output of print() through this callback. */
static void script_pkpy_print_handler(const char *s) {
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("%s"), s);
}


static void script_pkpy_task(void *p) {
	ScriptPkpy *self = (ScriptPkpy *)p;

	/* Snapshot the free heap before the interpreter allocates anything. malloc()/pvPortMalloc()
	 * share the same newlib heap here, so xPortGetFreeHeapSize() accounts for pocketpy too. */
	size_t heap_before = xPortGetFreeHeapSize();
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("free heap before: %u bytes"), (unsigned int)heap_before);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initializing pocketpy interpreter"));
	py_initialize();
	py_callbacks()->print = script_pkpy_print_handler;

	/* EVAL_MODE so the trailing expression value ends up in py_retval(). */
	bool ok = py_exec(script, "<string>", EVAL_MODE, NULL);
	if (ok) {
		if (py_isint(py_retval())) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("result: %d"), (int)py_toint(py_retval()));
		} else {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("script finished (non-int result)"));
		}
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("script execution failed"));
		py_printexc();
	}

	/* Snapshot the free heap again with the VM and its objects still alive. The difference is the
	 * heap RAM the interpreter is holding; note the statically allocated default VM (including its
	 * value stack) lives in .bss and is therefore not reflected here. */
	size_t heap_after = xPortGetFreeHeapSize();
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("free heap after:  %u bytes"), (unsigned int)heap_after);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("heap used by interpreter: %u bytes"),
	      (unsigned int)(heap_before - heap_after));

	/* Minimum free stack (untouched 0xa5 paint) to help right-size the task stack. */
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stack high water mark: %u bytes free"),
	      (unsigned int)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

	py_finalize();

	/* Keep the task around so its stack usage can still be inspected. */
	while (true) {
		vTaskDelay(1000);
	}

	vTaskDelete(NULL);
}


script_pkpy_ret_t script_pkpy_init(ScriptPkpy *self) {
	if (u_assert(self != NULL)) {
		return SCRIPT_PKPY_RET_NULL;
	}
	memset(self, 0, sizeof(ScriptPkpy));

	xTaskCreate(script_pkpy_task, "script-pkpy", configMINIMAL_STACK_SIZE + 1024, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return SCRIPT_PKPY_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module started"));

	return SCRIPT_PKPY_RET_OK;
}


script_pkpy_ret_t script_pkpy_free(ScriptPkpy *self) {
	if (u_assert(self != NULL)) {
		return SCRIPT_PKPY_RET_NULL;
	}

	return SCRIPT_PKPY_RET_OK;
}
