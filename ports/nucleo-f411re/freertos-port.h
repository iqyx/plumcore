#pragma once

#include "config.h"


extern uint32_t SystemCoreClock;
#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ ((TickType_t)CONFIG_FREERTOS_TICK_RATE_HZ)
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS port_task_timer_init
#define portGET_RUN_TIME_COUNTER_VALUE port_task_timer_get_value


