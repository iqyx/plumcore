#pragma once

#include "config.h"


#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TRACE_FACILITY 1
#define configUSE_MALLOC_FAILED_HOOK 1
#define configUSE_IDLE_HOOK 1
#define configUSE_TICK_HOOK 0
#ifdef __NVIC_PRIO_BITS
	#define configPRIO_BITS	__NVIC_PRIO_BITS
#else
	#define configPRIO_BITS	4
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 0xf
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define vPortSVCHandler sv_call_handler
#define xPortPendSVHandler pend_sv_handler
#define xPortSysTickHandler sys_tick_handler
#define configASSERT(x) if ((x) == 0) {taskDISABLE_INTERRUPTS(); for (;;);}

