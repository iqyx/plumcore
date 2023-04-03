/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus switch
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <main.h>
#include <interfaces/can.h>


typedef enum {
	NBUS_SWITCH_RET_OK = 0,
	NBUS_SWITCH_RET_FAILED,
} nbus_switch_ret_t;


typedef struct nbus_switch {
	Can *can1;
	Can *can2;
	Can *can3;

	/* RX thread */
	TaskHandle_t receive_task;
} NbusSwitch;


nbus_switch_ret_t nbus_switch_init(NbusSwitch *self, Can *can1, Can *can2, Can *can3);
