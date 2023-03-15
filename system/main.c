/*
 * plumCore entry point
 *
 * Copyright (c) 2015-2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "app.h"
#include "interfaces/servicelocator.h"
#include "services/plocator/plocator.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "system"

/* Statically allocate memory for the application instance. */
App app;
PLocator plocator;
IServiceLocator *locator;


static void init_task(void *p) {
	(void)p;

	/* First, initialize the service locator service. It is required to
	 * advertise any port-specific devices and interfaces. */
	if (plocator_init(&plocator) == PLOCATOR_RET_OK) {
		locator = plocator_iface(&plocator);
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("Platform initialization..."));
	platform_init();

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initializing port-specific components..."));
	port_init();

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initializing services..."));
	system_init();

	startup_banner();

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initializing application..."));
	app_init(&app);

	vTaskDelete(NULL);
}


int main(void) {
	platform_early_init();
	port_early_init();
	u_log_init();

	xTaskCreate(init_task, "init", configMINIMAL_STACK_SIZE + 512, NULL, 1, NULL);
	vTaskStartScheduler();

	/* Not reachable. */
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("scheduler failed"));
	while (1) {
		/* Cycle forever. The watchdog will reset the board soon. */
		;
	}
}





