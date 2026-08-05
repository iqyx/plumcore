/* SPDX-License-Identifier: BSD-2-Clause
 *
 * ARM Cortex-M33 platform specific code
 *
 * Copyright (c) 2015-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include "main.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "cortex-m33"


void platform_early_init(void) {

}


void platform_init(void) {

}


void vApplicationMallocFailedHook(void);
void vApplicationMallocFailedHook(void) {
	/* This error should not cause system malfunction. The service or module must
	 * handle allocation errors properly and the rest of the system should not
	 * be compromised. */
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("failed to allocate memory"));
}


void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName);
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
	(void)pxTask;

	/* This is somewhat tough to deal with. Cycle forever and let the watchdog
	 * reset the board. */
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("stack overflow detected in task '%s'"), pcTaskName);
	while (1) {
		;
	}
}
