/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 FDCAN interface driver
 *
 * Copyright (c) 2016-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libopencm3/stm32/fdcan.h>

#include <main.h>
#include <interfaces/can.h>
#include "stm32-fdcan.h"

#define MODULE_NAME "fdcan-stm32"


static can_ret_t stm32_fdcan_send(Can *can, const struct can_message *msg, uint32_t timeout_ms) {
	Stm32Fdcan *self = (Stm32Fdcan *)can;

	/** @todo replace with  semaphore wait */
	(void)timeout_ms;
	while (!fdcan_available_tx(self->fdcan)) {
		;
	}
	if (fdcan_transmit(self->fdcan, msg->id, true, false, false, false, msg->len, msg->buf) != FDCAN_E_OK) {
		return CAN_RET_FAILED;
	}

	return CAN_RET_OK;
}


static can_ret_t stm32_fdcan_receive(Can *can, struct can_message *msg, uint32_t timeout_ms) {
	Stm32Fdcan *self = (Stm32Fdcan *)can;
	/* Wait on the semaphore until a CAN frame arrives. The semaphore is signalled
	 * from the IRQ handler function. */
	xSemaphoreTake(self->rx_sem, pdMS_TO_TICKS(timeout_ms));

	uint8_t rx_fmi;
	uint8_t rx_length;
	uint16_t rx_timestamp;

	if (fdcan_receive(self->fdcan, FDCAN_FIFO0, true, &msg->id, &msg->extid, &msg->rtr, &rx_fmi, &rx_length, msg->buf, &rx_timestamp) != FDCAN_E_OK) {
		return CAN_RET_FAILED;
	}
	msg->len = rx_length;
	msg->timestamp = rx_timestamp;

	return CAN_RET_OK;
}


static const struct can_vmt stm32_fdcan_vmt = {
	.send = stm32_fdcan_send,
	.receive = stm32_fdcan_receive
};


stm32_fdcan_ret_t stm32_fdcan_init(Stm32Fdcan *self, uint32_t fdcan) {
	memset(self, 0, sizeof(Stm32Fdcan));
	self->fdcan = fdcan;

	self->rx_sem = xSemaphoreCreateBinary();
	if (self->rx_sem == NULL) {
		return STM32_FDCAN_RET_FAILED;
	}

	/* Setup the CAN interface now. */
	self->iface.vmt = &stm32_fdcan_vmt;
	self->iface.parent = self;

	return STM32_FDCAN_RET_OK;
}

stm32_fdcan_ret_t stm32_fdcan_free(Stm32Fdcan *self) {
	vSemaphoreDelete(self->rx_sem);

	return STM32_FDCAN_RET_OK;
}


stm32_fdcan_ret_t stm32_fdcan_irq_handler(Stm32Fdcan *self) {
	if (FDCAN_IR(self->fdcan) & FDCAN_IR_RF0N) {
		FDCAN_IR(self->fdcan) |= FDCAN_IR_RF0N;

		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		if (self->rx_sem != NULL) {
			xSemaphoreGiveFromISR(self->rx_sem, &xHigherPriorityTaskWoken);
		}
		portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
	if (FDCAN_IR(self->fdcan) & FDCAN_IR_RF0F) {
		FDCAN_IR(self->fdcan) |= FDCAN_IR_RF0F;
	}

	return STM32_FDCAN_RET_OK;
}
