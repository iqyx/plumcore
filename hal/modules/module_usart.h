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

#ifndef _MODULE_USART_H_
#define _MODULE_USART_H_


#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "interface_stream.h"


#define MODULE_USART_RX_QUEUE_LEN 32
#define MODULE_USART_TX_QUEUE_LEN 32

struct module_usart {
	struct interface_stream iface;

	/**
	 * libopencm3 usart port
	 */
	uint32_t port;

	QueueHandle_t rxqueue;
	QueueHandle_t txqueue;

};


void module_usart_interrupt_handler(struct module_usart *usart);

int32_t module_usart_init(struct module_usart *usart, const char *name, uint32_t port);
#define MODULE_USART_INIT_OK 0
#define MODULE_USART_INIT_FAILED -1

int32_t module_usart_free(struct module_usart *usart);
#define MODULE_USART_FREE_OK 0
#define MODULE_USART_FREE_FAILED -1

int32_t module_usart_set_baudrate(struct module_usart *usart, uint32_t baudrate);
#define MODULE_USART_SET_BAUDRATE_OK 0
#define MODULE_USART_SET_BAUDRATE_FAILED -1


#endif
