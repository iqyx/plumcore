/*
 * ARM Cortex-M4F platform specific code
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


#include "main.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "platform"


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
