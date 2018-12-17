#pragma once

#include "config.h"
#include "freertos-port.h"
#include "freertos-platform.h"

/** @todo Generic FreeRTOS features should by selected by the
 *        scheduler/IPC configuration */

#define configMAX_PRIORITIES (CONFIG_FREERTOS_MAX_PRIORITIES)
#define configMINIMAL_STACK_SIZE		((unsigned short)CONFIG_FREERTOS_MINIMAL_STACK_SIZE)
#define configMAX_TASK_NAME_LEN			(CONFIG_FREERTOS_MAX_TASK_NAME_LEN)
#define configUSE_PREEMPTION defined(CONFIG_FREERTOS_USE_PREEMPTION)
#define configUSE_APPLICATION_TASK_TAG	0
#define configUSE_COUNTING_SEMAPHORES	1
#define configGENERATE_RUN_TIME_STATS	1
#define configUSE_CO_ROUTINES 		0
#define configMAX_CO_ROUTINE_PRIORITIES ( 2 )
#define configUSE_TIMERS				1
#define configTIMER_TASK_PRIORITY		( 2 )
#define configTIMER_QUEUE_LENGTH		10
#define configTIMER_TASK_STACK_DEPTH	( configMINIMAL_STACK_SIZE * 2 )
#define INCLUDE_vTaskPrioritySet		1
#define INCLUDE_uxTaskPriorityGet		1
#define INCLUDE_vTaskDelete				1
#define INCLUDE_vTaskCleanUpResources	1
#define INCLUDE_vTaskSuspend			1
#define INCLUDE_vTaskDelayUntil			1
#define INCLUDE_vTaskDelay				1
#define configUSE_MUTEXES defined(CONFIG_FREERTOS_USE_MUTEXES)
#define configQUEUE_REGISTRY_SIZE CONFIG_FREERTOS_QUEUE_REGISTRY_SIZE
#if defined(CONFIG_FREERTOS_CHECK_FOR_STACK_OVERFLOW)
	#if defined(CONFIG_FREERTOS_CHECK_FOR_STACK_OVERFLOW_METHOD2)
		#define configCHECK_FOR_STACK_OVERFLOW	2
	#else
		#define configCHECK_FOR_STACK_OVERFLOW	1
	#endif
#else
	#define configCHECK_FOR_STACK_OVERFLOW	0
#endif
#define configUSE_RECURSIVE_MUTEXES defined(CONFIG_FREERTOS_USE_RECURSIVE_MUTEXES)





