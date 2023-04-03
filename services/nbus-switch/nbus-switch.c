/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus switch
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libopencm3/stm32/fdcan.h>
#include <libopencm3/stm32/gpio.h>
#include <main.h>
#include <interfaces/can.h>
#include "nbus-switch.h"

#define MODULE_NAME "nbus-switch"


static nbus_switch_ret_t nbus_switch_receive(Can *can, struct can_message *msg) {

	if (can == NULL || can->vmt->receive == NULL) {
		return NBUS_SWITCH_RET_FAILED;
	}

	if (can->vmt->receive(can, msg, 0) != CAN_RET_OK) {
		return NBUS_SWITCH_RET_FAILED;
	}

	if (msg->extid == false) {
		return NBUS_SWITCH_RET_FAILED;
	}

	return NBUS_SWITCH_RET_OK;
}


static nbus_switch_ret_t nbus_switch_send(Can *can, struct can_message *msg) {
	if (can == NULL || can->vmt->send == NULL) {
		return NBUS_SWITCH_RET_FAILED;
	}
	msg->extid = true;
	can->vmt->send(can, msg, 100);
	return NBUS_SWITCH_RET_OK;
}

static void nbus_switch_receive_task(void *p) {
	NbusSwitch *self = (NbusSwitch *)p;

	struct can_message msg = {0};
	while (true) {
		if (nbus_switch_receive(self->can1, &msg) == NBUS_SWITCH_RET_OK) {
			gpio_set(GPIOB, GPIO14);
			nbus_switch_send(self->can2, &msg);
			// nbus_switch_send(self->can3, &msg);
		}
		if (nbus_switch_receive(self->can2, &msg) == NBUS_SWITCH_RET_OK) {
			nbus_switch_send(self->can1, &msg);
		}
		// if (nbus_switch_receive(self->can3, &msg) == NBUS_SWITCH_RET_OK) {
			// nbus_switch_send(self->can1, &msg);
		// }
		gpio_clear(GPIOB, GPIO14);
	}
	vTaskDelete(NULL);
}


nbus_switch_ret_t nbus_switch_init(NbusSwitch *self, Can *can1, Can *can2, Can *can3) {
	memset(self, 0, sizeof(NbusSwitch));
	self->can1 = can1;
	self->can2 = can2;
	self->can3 = can3;

	/* We need to respond fast to NBUS requests over CAN-FD. Set to a higher priority. */
	xTaskCreate(nbus_switch_receive_task, "nbus-sw-task", configMINIMAL_STACK_SIZE + 256, (void *)self, 2, &(self->receive_task));
	if (self->receive_task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot create task"));
		goto err;
	}

	return NBUS_SWITCH_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("init failed"));
	return NBUS_SWITCH_RET_FAILED;
}
