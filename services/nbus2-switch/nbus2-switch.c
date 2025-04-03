/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nbus2 protocol switch
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <main.h>
#include <interfaces/datagram.h>
#include <libopencm3/stm32/gpio.h>

#include "nbus2-switch.h"

#define MODULE_NAME "nbus2-switch"


static void nbus2_switch_housekeeping_task(void *p) {
	Nbus2Switch *self = (Nbus2Switch *)p;

	while (true) {
		/** @todo */


		/* Blink all LEDs slowly to show active state but no data being transmitted/received. */
		for (size_t i = 0; i < NBUS2_SWITCH_MAX_PORTS; i++) {
			if (self->ports[i].parent != NULL && self->ports[i].locm3_led_port) {
				gpio_toggle(self->ports[i].locm3_led_port, self->ports[i].locm3_led_pin);
			}
		}

		vTaskDelay(1000);
	}
}


static void nbus2_switch_receive_task(void *p) {
	struct nbus2_switch_port *port = (struct nbus2_switch_port *)p;
	Nbus2Switch *self = port->parent;

	uint8_t *packet_buffer = malloc(NBUS2_SWITCH_MTU);
	if (packet_buffer == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate packet buffer for RX task"));
		vTaskDelete(NULL);
	}

	while (true) {
		struct datagram_msg rxmsg = {0};
		size_t len = NBUS2_SWITCH_MTU;
		if (port->datagram->vmt->read(port->datagram, packet_buffer, &len, &rxmsg) == DATAGRAM_RET_OK) {
			/* Blink the source port LED. */
			if (port->locm3_led_port) {
				gpio_toggle(port->locm3_led_port, port->locm3_led_pin);
			}

			//struct datagram_msg txmsg = {
				//.addr_size = rxmsg.addr_size,
				//.dst_port = rxmsg.src_port,
				//.src_port = rxmsg.dst_port,
			//};
			//memcpy(txmsg.dst_addr, rxmsg.src_addr, rxmsg.addr_size);
			//memcpy(txmsg.src_addr, rxmsg.dst_addr, rxmsg.addr_size);

			/** @todo write to other ports and the local port */
			for (size_t i = 0; i < NBUS2_SWITCH_MAX_PORTS; i++) {
				if (self->ports[i].parent != NULL && port != &(self->ports[i])) {
					self->ports[i].datagram->vmt->write(self->ports[i].datagram, packet_buffer, len, &rxmsg);
					if (self->ports[i].locm3_led_port) {
						gpio_toggle(self->ports[i].locm3_led_port, self->ports[i].locm3_led_pin);
					}
				}
			}


			/* Continue immediately on success. */
			continue;
		}

		/* Wait a bit on error to avoid looping on the wrong type of error. */
		vTaskDelay(10);

	}
	vTaskDelete(NULL);
}


nbus2_switch_ret_t nbus2_switch_init(Nbus2Switch *self) {
	memset(self, 0, sizeof(Nbus2Switch));

	xTaskCreate(nbus2_switch_housekeeping_task, "nbus2-hk", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->housekeeping_task));
	if (self->housekeeping_task == NULL) {
		return NBUS2_SWITCH_RET_FAILED;
	}

	return NBUS2_SWITCH_RET_OK;
}


nbus2_switch_ret_t nbus2_switch_add_port(Nbus2Switch *self, Datagram *datagram, uint32_t locm3_led_port, uint32_t locm3_led_pin) {
	/** @todo locking */
	struct nbus2_switch_port *port = NULL;
	for (uint32_t i = 0; i < NBUS2_SWITCH_MAX_PORTS; i++) {
		if (self->ports[i].parent == NULL) {
			port = &(self->ports[i]);
			break;
		}
	}
	if (port == NULL) {
		return NBUS2_SWITCH_RET_FAILED;
	}

	port->datagram = datagram;
	port->locm3_led_port = locm3_led_port;
	port->locm3_led_pin = locm3_led_pin;
	port->parent = self;

	xTaskCreate(nbus2_switch_receive_task, "nbus2-sw", configMINIMAL_STACK_SIZE + 128, (void *)port, 1, &(port->receive_task));
	if (port->receive_task == NULL) {
		port->parent = NULL;
		port->datagram = NULL;
		return NBUS2_SWITCH_RET_FAILED;
	}

	return NBUS2_SWITCH_RET_OK;
}


