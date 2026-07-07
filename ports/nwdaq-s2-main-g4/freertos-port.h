#pragma once

#include "config.h"

extern uint32_t SystemCoreClock;
void port_task_timer_init(void);
uint32_t port_task_timer_get_value(void);
#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ ((TickType_t)CONFIG_FREERTOS_TICK_RATE_HZ)
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS port_task_timer_init
#define portGET_RUN_TIME_COUNTER_VALUE port_task_timer_get_value
/* The newlib sbrk heap (see platforms/cortex-m4f/heap_useNewlib.c) grows up from _ebss and is hard
 * capped at _ebss + configTOTAL_HEAP_SIZE; the MSP exception stack grows down from the top of RAM
 * (0x2001C000). With ~6 KiB of static data, 102 KiB of heap leaves ~4 KiB of headroom for the MSP
 * stack (adequate given this port only takes shallow, non-nested interrupts). All FreeRTOS task
 * stacks are also allocated from this heap. */
#define configTOTAL_HEAP_SIZE ((size_t)(102 * 1024))
