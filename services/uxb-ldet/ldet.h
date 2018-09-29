/*
 * plumCore lightning detector UXB driver
 *
 * Copyright (C) 2018, Marek Koza, qyx@krtko.org
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

#include "FreeRTOS.h"
#include "task.h"
#include "module.h"
#include "libuxb.h"
#include "interfaces/sensor.h"


typedef enum {
	UXB_LDET_RET_OK = 0,
	UXB_LDET_RET_FAILED = -1,
} uxb_ldet_ret_t;


typedef struct {
	Module module;

	IUxbSlot *slot;
	uint8_t slot_buffer[128];

	TaskHandle_t receive_task;
	bool receive_can_run;

	/* Some statistics. */
	uint32_t signals_total;
	int32_t ema_level_rms;
	int32_t ema_level_peak;

	ISensor sensor_signals_total;
	ISensor sensor_ema_level_rms;
	ISensor sensor_ema_level_peak;


} UxbLdet;


uxb_ldet_ret_t uxb_ldet_init(UxbLdet *self, IUxbSlot *slot);
uxb_ldet_ret_t uxb_ldet_free(UxbLdet *self);


