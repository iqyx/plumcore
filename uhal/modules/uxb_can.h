/*
 * plumCore CAN interface UXB driver
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

#include "FreeRTOS.h"
#include "task.h"
#include "module.h"
#include "interfaces/sensor.h"
#include "libuxb.h"

#include "interfaces/hcan.h"


typedef enum {
	UXB_CAN_RET_OK = 0,
	UXB_CAN_RET_FAILED = -1,
} uxb_can_ret_t;


typedef struct {
	Module module;

	IUxbSlot *slot;
	uint8_t slot_buffer[64];

	TaskHandle_t receive_task;
	bool receive_can_run;

	HCan can_host;

} UxbCan;


uxb_can_ret_t uxb_can_init(UxbCan *self, IUxbSlot *slot);
uxb_can_ret_t uxb_can_free(UxbCan *self);

