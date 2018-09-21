/*
 * Independent watchdog service
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
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
#include <stdio.h>

#include <libopencm3/stm32/iwdg.h>

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "watchdog.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "watchdog"


static void watchdog_task(void *p) {
	Watchdog *self = (Watchdog *)p;

	while (1) {
		iwdg_reset();
		vTaskDelay(self->period_ms / 2);
	}
}


watchdog_ret_t watchdog_init(Watchdog *self, uint32_t period_ms, int32_t priority) {
	if (u_assert(self != NULL)) {
		return WATCHDOG_RET_FAILED;
	}

	memset(self, 0, sizeof(Watchdog));

	self->period_ms = period_ms;
	self->priority = priority;

	xTaskCreate(watchdog_task, "watchdog", configMINIMAL_STACK_SIZE, (void *)self, self->priority, &(self->task));
	if (self->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		return WATCHDOG_RET_FAILED;
	}

	iwdg_set_period_ms(self->period_ms);
	iwdg_start();
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("started, period = %ums, prio = %d"), self->period_ms, self->priority);

	return WATCHDOG_RET_OK;
}


watchdog_ret_t watchdog_free(Watchdog *self) {
	if (u_assert(self != NULL)) {
		return WATCHDOG_RET_FAILED;
	}

	/* The watchdog cannot be disabled. */
	return WATCHDOG_RET_FAILED;
}

