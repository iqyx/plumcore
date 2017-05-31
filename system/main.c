/**
 * uMeshFw entry point and custom FreeRTOS hooks
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
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

#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/cm3/scb.h>
#include "FreeRTOS.h"
#include "task.h"
#include "port.h"

#include "u_assert.h"
#include "u_log.h"


static void init_task(void *p) {
	(void)p;

	port_init();

	vTaskDelete(NULL);
}


static void test_task(void *p) {
	(void)p;

	vTaskDelay(1000);
	vTaskDelete(NULL);
}


int main(void) {

	port_early_init();
	u_log_init();

	xTaskCreate(init_task, "init", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
	xTaskCreate(test_task, "test", configMINIMAL_STACK_SIZE + 256, NULL, 1, NULL);
	vTaskStartScheduler();

	/* Not reachable. */
	while (1) {
		;
	}
}


void vApplicationMallocFailedHook(void);
void vApplicationMallocFailedHook(void) {
	;
}


void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName);
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
	(void)pxTask;
	u_log(system_log, LOG_TYPE_CRIT, "core: stack overflow detected in task '%s'", pcTaskName);
	while (1) {
		;
	}
}


void vApplicationIdleHook(void);
void vApplicationIdleHook(void) {
	__asm("wfi");
}


void sys_tick_handler(void);
void sys_tick_handler(void) {
	/* Patch to prevent HardFaults in FreeRTOS when the systick interrupt
	 * occurs before the scheduler is started.
	 * https://my.st.com/public/STe2ecommunities/mcu/Lists/STM32Java/Flat.aspx?
	 * RootFolder=%2Fpublic%2FSTe2ecommunities%2Fmcu%2FLists%2FSTM32Java%2FCube
	 * MX%204.3.1%20-%20FreeRTOS%20SysTick%20and%20HardFault&FolderCTID=0x01200
	 * 200770978C69A1141439FE559EB459D758000F9A0E3A95BA69146A17C2E80209ADC21
	 * &currentviews=310 */
	if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
		xPortSysTickHandler();
	}
}


void hard_fault_hook(uint32_t *stack) __attribute__((used));
void hard_fault_hook(uint32_t *stack) {
	u_log(system_log, LOG_TYPE_CRIT, "core: processor hard fault at 0x%08x", stack[6]);
	u_log(system_log, LOG_TYPE_CRIT, "core: r0 = 0x%08x, r1 = 0x%08x, r2 = 0x%08x, r3 = 0x%08x", stack[0], stack[1], stack[2], stack[3]);
	u_log(system_log, LOG_TYPE_CRIT, "core: r12 = 0x%08x, lr = 0x%08x, pc = 0x%08x, psr = 0x%08x", stack[4], stack[5], stack[6], stack[7]);
	u_log(system_log, LOG_TYPE_CRIT, "core: cfsr = 0x%08x, hfsr = 0x%08x, dfsr = 0x%08x, afsr = 0x%08x",
		(*((volatile unsigned long *)(0xE000ED28))),
		(*((volatile unsigned long *)(0xE000ED2C))),
		(*((volatile unsigned long *)(0xE000ED30))),
		(*((volatile unsigned long *)(0xE000ED3C)))
	);
	u_log(system_log, LOG_TYPE_CRIT, "core: mmar = 0x%08x, bfar = 0x%08x",
		(*((volatile unsigned long *)(0xE000ED34))),
		(*((volatile unsigned long *)(0xE000ED38)))
	);

	/* TODO: try to recover from hardfault - suspend/kill this task only
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

