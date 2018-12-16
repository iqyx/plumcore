/**
 * uMeshFw libopencm3 USART module
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
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

#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "u_assert.h"
#include "u_log.h"
#include "interface_stream.h"
#include "module_usart.h"


void module_usart_interrupt_handler(struct module_usart *usart) {

	if ((USART_SR(usart->port) & USART_SR_RXNE)) {
		/* The byte is already received, get it. */
		uint8_t byte = usart_recv(usart->port);

		if (usart->rxqueue != NULL) {
			/* Do not check the return value. If the queue is full, the received
			 * byte will be lost. */
			xQueueSendToBackFromISR(usart->rxqueue, &byte, 0);
		}
	}
}


static int32_t module_usart_read_timeout(void *context, uint8_t *buf, uint32_t len, uint32_t timeout) {
	if (u_assert(context != NULL && len > 0)) {
		return -1;
	}
	struct module_usart *usart = (struct module_usart *)context;

	/* Wait for at least one character. */
	if (xQueuePeek(usart->rxqueue, &buf[0], timeout) == pdFALSE) {
		return 0;
	}

	uint32_t read = 0;
	while (read < len && xQueueReceive(usart->rxqueue, &buf[read], 0) == pdTRUE) {
		read++;
	}

	return read;
}


static int32_t module_usart_read(void *context, uint8_t *buf, uint32_t len) {

	return module_usart_read_timeout(context, buf, len, portMAX_DELAY);
}


static int32_t module_usart_write(void *context, const uint8_t *buf, uint32_t len) {
	if (u_assert(context != NULL)) {
		return -1;
	}
	struct module_usart *usart = (struct module_usart *)context;

	/* TODO: replace */
	for (uint32_t i = 0; i < len; i++) {
		usart_send_blocking(usart->port, buf[i]);
	}

	return -1;
}


int32_t module_usart_init(struct module_usart *usart, const char *name, uint32_t port) {
	if (u_assert(usart != NULL)) {
		return MODULE_USART_INIT_FAILED;
	}
	(void)name;

	memset(usart, 0, sizeof(struct module_usart));

	/* Initialize stream interface. */
	interface_stream_init(&(usart->iface));
	usart->iface.vmt.context = (void *)usart;
	usart->iface.vmt.read = module_usart_read;
	usart->iface.vmt.read_timeout = module_usart_read_timeout;
	usart->iface.vmt.write = module_usart_write;

	usart->port = port;
	/* Defaults only, can be changed using usart API. */
	usart_set_baudrate(usart->port, 115200);
	usart_set_mode(usart->port, USART_MODE_TX_RX);
	usart_set_databits(usart->port, 8);
	usart_set_stopbits(usart->port, USART_STOPBITS_1);
	usart_set_parity(usart->port, USART_PARITY_NONE);
	usart_set_flow_control(usart->port, USART_FLOWCONTROL_NONE);
	usart_enable(usart->port);

	usart->rxqueue = xQueueCreate(MODULE_USART_RX_QUEUE_LEN, sizeof(uint8_t));
	usart->txqueue = xQueueCreate(MODULE_USART_TX_QUEUE_LEN, sizeof(uint8_t));

	if (usart->rxqueue == NULL || usart->txqueue == NULL) {
		goto err;
	}

	/* Interrupts can be enabled now. */
	usart_enable_rx_interrupt(usart->port);

	u_log(system_log, LOG_TYPE_INFO, "module USART initialized using default settings");
	return MODULE_USART_INIT_OK;

err:
	module_usart_free(usart);
	u_log(system_log, LOG_TYPE_ERROR, "module USART failed to initialize");
	return MODULE_USART_INIT_FAILED;
}


int32_t module_usart_free(struct module_usart *usart) {
	if (u_assert(usart != NULL)) {
		return MODULE_USART_FREE_FAILED;
	}

	if (usart->rxqueue != NULL) {
		vQueueDelete(usart->rxqueue);
		usart->rxqueue = NULL;
	}
	if (usart->txqueue != NULL) {
		vQueueDelete(usart->txqueue);
		usart->txqueue = NULL;
	}

	return MODULE_USART_FREE_OK;
}
