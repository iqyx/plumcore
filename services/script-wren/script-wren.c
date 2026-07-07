/* SPDX-License-Identifier: BSD-2-Clause
 *
 * wren.io language interpreter PoC service
 *
 * Runs a small Wren script on the Wren VM and reports its footprint: free heap before and after
 * execution (RAM usage), the task stack high water mark (stack usage) and, indirectly, the flash
 * usage which can be read from the linker size output. This mirrors the other script-* PoC services
 * (script-es, script-pkpy, script-gravity) used to compare embeddable interpreters.
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

/* Small Wren program executed on the VM. */
static const char *script = "System.print(\"I am running in a VM!\")";


/* Wren routes the output of System.print() through this callback. */
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
}


static void script_wren_task(void *p) {
	ScriptWren *self = (ScriptWren *)p;

	/* Snapshot the free heap before the interpreter allocates anything. Wren's default allocator
	 * uses realloc()/free(), which share the same newlib heap as pvPortMalloc() here, so
	 * xPortGetFreeHeapSize() accounts for the VM too. */
	size_t heap_before = xPortGetFreeHeapSize();
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("free heap before: %u bytes"), (unsigned int)heap_before);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initializing Wren interpreter"));

	WrenConfiguration config;
	wrenInitConfiguration(&config);
	config.writeFn = &script_wren_stdout_handler;
	config.errorFn = &script_wren_error_handler;

	/* Wren's defaults (10 MB before the first GC, 1 MB minimum) target desktops and would let the VM
	 * consume the whole MCU heap before the collector ever runs. Compiling the built-in core library
	 * alone allocates tens of kB of transient objects that are only reclaimed by a GC pass, so on this
	 * device the VM must collect early and often. Trigger the first GC at 16 kB, keep the next-GC
	 * threshold from dropping below 8 kB, and grow it by only 25 % of the live set after each pass. */
	config.initialHeapSize = 16 * 1024;
	config.minHeapSize = 8 * 1024;
	config.heapGrowthPercent = 25;

	self->vm = wrenNewVM(&config);
	if (self->vm != NULL) {
		const char *module = "main";

		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("running the sample script"));
		WrenInterpretResult result = wrenInterpret(self->vm, module, script);
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("result %d"), result);
	} else {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create Wren VM"));
	}

	/* Snapshot the free heap again with the VM and its objects still alive. The difference is the
	 * heap RAM the interpreter is holding. */
	size_t heap_after = xPortGetFreeHeapSize();
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("free heap after:  %u bytes"), (unsigned int)heap_after);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("heap used by interpreter: %u bytes"),
	      (unsigned int)(heap_before - heap_after));

	/* Minimum free stack (untouched 0xa5 paint) to help right-size the task stack. */
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stack high water mark: %u bytes free"),
	      (unsigned int)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

	/* Keep the task around so its stack usage can still be inspected. */
	while (true) {
		vTaskDelay(1000);
	}

	wrenFreeVM(self->vm);
	vTaskDelete(NULL);
}


script_wren_ret_t script_wren_init(ScriptWren *self) {
	if (u_assert(self != NULL)) {
		return SCRIPT_WREN_RET_NULL;
	}
	memset(self, 0, sizeof(ScriptWren));

	/* Wren's single-pass compiler nests a Compiler struct on the C stack for every level. The lib is
	 * built with MAX_LOCALS/MAX_UPVALUES lowered to 64 (see lib/wren/SConscript), shrinking that
	 * struct from ~6.2 kB to ~1.6 kB; compiling the core library reaches three levels (module ->
	 * method -> the `sort` closure) plus expression recursion, so the peak is ~7.5 kB. 10 kB keeps a
	 * safe margin while returning as much of the heap as possible to the (large) core VM. */
	xTaskCreate(script_wren_task, "script-wren", configMINIMAL_STACK_SIZE + 2500, (void *)self, 1, &(self->task));
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
