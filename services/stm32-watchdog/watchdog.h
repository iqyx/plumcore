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

#pragma once

#include <stdint.h>
#include "module.h"


typedef enum {
	WATCHDOG_RET_OK = 0,
	WATCHDOG_RET_FAILED = -1,
} watchdog_ret_t;

typedef struct {
	Module module;

	TaskHandle_t task;
	uint32_t period_ms;
	int32_t priority;

} Watchdog;


watchdog_ret_t watchdog_init(Watchdog *self, uint32_t period_ms, int32_t priority);
watchdog_ret_t watchdog_free(Watchdog *self);
