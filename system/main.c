/*
 * plumCore entry point and custom FreeRTOS hooks
 *
 * Copyright (C) 2015-2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "port.h"
#include "system.h"

#include "u_assert.h"
#include "u_log.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "system"


static void init_task(void *p) {
	(void)p;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initializing port-specific components..."));
	port_init();

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initializing services..."));
	system_init();

	vTaskDelete(NULL);
}


int main(void) {
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


void vApplicationIdleHook(void);
void vApplicationIdleHook(void) {
	__asm("wfi");
}


void hard_fault_hook(uint32_t *stack) __attribute__((used));
void hard_fault_hook(uint32_t *stack) {
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("processor hard fault at 0x%08x"), stack[6]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("r0 = 0x%08x, r1 = 0x%08x, r2 = 0x%08x, r3 = 0x%08x"), stack[0], stack[1], stack[2], stack[3]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("r12 = 0x%08x, lr = 0x%08x, pc = 0x%08x, psr = 0x%08x"), stack[4], stack[5], stack[6], stack[7]);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("cfsr = 0x%08x, hfsr = 0x%08x, dfsr = 0x%08x, afsr = 0x%08x"),
		(*((volatile unsigned long *)(0xE000ED28))),
		(*((volatile unsigned long *)(0xE000ED2C))),
		(*((volatile unsigned long *)(0xE000ED30))),
		(*((volatile unsigned long *)(0xE000ED3C)))
	);
	u_log(system_log, LOG_TYPE_CRIT, U_LOG_MODULE_PREFIX("mmar = 0x%08x, bfar = 0x%08x"),
		(*((volatile unsigned long *)(0xE000ED34))),
		(*((volatile unsigned long *)(0xE000ED38)))
	);

	/** @todo try to recover from the hardfault - suspend/kill this task only
	 * and do a context switch. Task may be restarted later by the process
	 * manager or the whole system can be restarted by the watchdog. */
	while (1) {
		;
	}
}


/* TODO: move to port/platform specific code. */
void hard_fault_handler(void) __attribute__((naked));
void hard_fault_handler(void)
{
    __asm volatile
    (
        " tst lr, #4                                                \n"
        " ite eq                                                    \n"
        " mrseq r0, msp                                             \n"
        " mrsne r0, psp                                             \n"
        " ldr r1, [r0, #24]                                         \n"
        " ldr r2, handler2_address_const                            \n"
        " bx r2                                                     \n"
        " handler2_address_const: .word hard_fault_hook             \n"

    );
    //~ return;
}

