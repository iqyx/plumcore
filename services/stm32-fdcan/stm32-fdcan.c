/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32 FDCAN interface driver
 *
 * Copyright (c) 2016-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libopencm3/stm32/fdcan.h>

#include <main.h>
#include <interfaces/can.h>
#include "stm32-fdcan.h"

#define MODULE_NAME "stm32-fdcan"


static can_ret_t stm32_fdcan_send(Can *can, const struct can_message *msg, uint32_t timeout_ms) {
	Stm32Fdcan *self = (Stm32Fdcan *)can;

	if (FDCAN_PSR(self->fdcan) & FDCAN_PSR_BO) {
		/* Clear the INIT bit to request recovery from the bus-off state. */
		FDCAN_CCCR(self->fdcan) &= ~FDCAN_CCCR_INIT;
		return CAN_RET_BUS_OFF;
	}

	/* Wait for a free transmit slot, bounded by the requested timeout. */
	TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
	while (!fdcan_available_tx(self->fdcan)) {
		if (xTaskGetTickCount() >= deadline) {
			return CAN_RET_TIMEOUT;
		}
		vTaskDelay(1);
	}

	if (fdcan_transmit(self->fdcan, msg->id, msg->extid, msg->rtr, false, false, msg->len, msg->buf) < 0) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("transmit error"));
		return CAN_RET_FAILED;
	}

	return CAN_RET_OK;
}


static can_ret_t stm32_fdcan_receive(Can *can, struct can_message *msg, uint32_t timeout_ms) {
	Stm32Fdcan *self = (Stm32Fdcan *)can;

	/* Wait on the semaphore until a CAN frame arrives. The semaphore is signalled
	 * from the IRQ handler function. */
	if (xSemaphoreTake(self->rx_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
		return CAN_RET_TIMEOUT;
	}

	uint8_t rx_fmi;
	uint8_t rx_length;
	uint16_t rx_timestamp;
	if (fdcan_receive(self->fdcan, FDCAN_FIFO0, true, &msg->id, &msg->extid, &msg->rtr,
	                  &rx_fmi, &rx_length, msg->buf, &rx_timestamp) != FDCAN_E_OK) {
		return CAN_RET_FAILED;
	}
	msg->len = rx_length;
	msg->timestamp = rx_timestamp;

	/* Check if the fifo is empty. If not, signal the semaphore again. */
	uint32_t frames = (FDCAN_RXFIS(self->fdcan, FDCAN_FIFO0) >> FDCAN_RXFIFO_FL_SHIFT) & FDCAN_RXFIFO_FL_MASK;
	if (frames > 0) {
		xSemaphoreGive(self->rx_sem);
	}

	return CAN_RET_OK;
}


static const struct can_vmt stm32_fdcan_vmt = {
	.send = stm32_fdcan_send,
	.receive = stm32_fdcan_receive,
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
