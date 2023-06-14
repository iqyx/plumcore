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
#include "services/nbus/nbus.h"

#define MODULE_NAME "nbus-switch"


static nbus_switch_ret_t find_channel(NbusSwitch *self, channel_t id, enum nbus_direction dir, struct nbus_switch_channel **ch) {
	/** @todo a more performant search */
	/* Find channel with a matching channel number. */
	struct nbus_switch_channel *found = NULL;
	for (uint32_t i = 0; i < NBUS_SWITCH_MAX_CHANNELS; i++) {
		if (self->ch[i].port != NULL && self->ch[i].ch == id && self->ch[i].dir == dir) {
			found = &(self->ch[i]);
			/* We are happy with the first one. */
			break;
		}
	}
	if (found == NULL) {
		return NBUS_SWITCH_RET_FAILED;
	}
	*ch = found;
	return NBUS_SWITCH_RET_OK;
}


static nbus_switch_ret_t add_channel(NbusSwitch *self, struct nbus_switch_channel **ch) {
	struct nbus_switch_channel *found = NULL;
	for (uint32_t i = 0; i < NBUS_SWITCH_MAX_CHANNELS; i++) {
		if (self->ch[i].port == NULL) {
			found = &(self->ch[i]);
			break;
		}
	}
	if (found == NULL) {
		/* No empty record is available to add the channel. */
		return NBUS_SWITCH_RET_FAILED;
	}
	*ch = found;
	return NBUS_SWITCH_RET_OK;
}


static nbus_switch_ret_t nbus_switch_send_port(NbusSwitch *self, struct nbus_switch_port *port, struct can_message *msg) {
	(void)self;
	if (port->can->vmt->send(port->can, msg, 100) != CAN_RET_OK) {
		port->tx_errors++;
		return NBUS_SWITCH_RET_FAILED;
	}
	port->tx_frames++;
	return NBUS_SWITCH_RET_OK;
}


/**
 * @brief Send to all allocated ports except the port @p except
 */
static nbus_switch_ret_t nbus_switch_send_multi(NbusSwitch *self, struct nbus_switch_port *except, struct can_message *msg) {
	for (uint32_t i = 0; i < NBUS_SWITCH_MAX_PORTS; i++) {
		if (self->ports[i].parent != NULL && &(self->ports[i]) != except) {
			nbus_switch_send_port(self, &(self->ports[i]), msg);
		}
	}
	return NBUS_SWITCH_RET_OK;
}


static nbus_switch_ret_t nbus_switch_process(NbusSwitch *self, struct nbus_switch_port *port, struct can_message *msg) {
	struct nbus_id sid = {0};
	nbus_parse_id(msg->id, &sid);

	// nbus_switch_send_multi(self, port, msg);
	// return NBUS_SWITCH_RET_OK;

	// if (sid.direction == NBUS_DIR_PUBLISH) {
	if (true) {
		/* Send to all ports except @p port. */
		nbus_switch_send_multi(self, port, msg);
	} else {
		/* Find the channel associated with the source of the message and add it if not found. */
		struct nbus_switch_channel *sch = NULL;
		if (find_channel(self, sid.channel, sid.direction, &sch) != NBUS_SWITCH_RET_OK) {
			if (add_channel(self, &sch) == NBUS_SWITCH_RET_OK) {
				sch->port = port;
				sch->dir = sid.direction;
				sch->ch = sid.channel;
				sch->frames = 0;
			}
		}
		if (sch != NULL) {
			if (sch->port != port) {
				/* Channel port has changed. Update it to the current one. */
				sch->port = port;
			}
			sch->last_access = 0;
			sch->frames++;
		}

		/* Now find the reverse direction - the destination of the current frame. */
		if (find_channel(self, sid.channel, sid.direction == NBUS_DIR_REQUEST ? NBUS_DIR_RESPONSE : NBUS_DIR_REQUEST, &sch) == NBUS_SWITCH_RET_OK) {
			nbus_switch_send_port(self, sch->port, msg);
			// sch->last_access = 0;
		} else {
			/* Not found yet. Do not add it because we don't have that information yet.
			 * Broadcast the frame instead and wait for the response. */
			nbus_switch_send_multi(self, port, msg);
		}
	}
	return NBUS_SWITCH_RET_OK;
}


static void nbus_switch_process_task(void *p) {
	NbusSwitch *self = (NbusSwitch *)p;

	while (true) {
		struct nbus_switch_iq_item item;
		if (xQueueReceive(self->iq, &item, portMAX_DELAY) == pdTRUE) {
			nbus_switch_process(self, item.port, &item.msg);
		}
	}
}


static void nbus_switch_housekeeping_task(void *p) {
	NbusSwitch *self = (NbusSwitch *)p;

	while (true) {
		gpio_toggle(GPIOB, GPIO14);

		for (uint32_t i = 0; i < NBUS_SWITCH_MAX_CHANNELS; i++) {
			if (self->ch[i].port != NULL) {
				self->ch[i].last_access++;
			}

			if (self->ch[i].last_access > NBUS_SWITCH_MAX_LIFETIME) {
				memset(&(self->ch[i]), 0, sizeof(struct nbus_switch_channel));
			}
		}

		vTaskDelay(1000);
	}
}


static void nbus_switch_receive_task(void *p) {
	struct nbus_switch_port *port = (struct nbus_switch_port *)p;
	NbusSwitch *self = port->parent;

	while (true) {
		gpio_toggle(GPIOB, GPIO15);

		/** @todo adjust the timeout */
		struct can_message msg = {0};
		if (port->can->vmt->receive(port->can, &msg, 2000) != CAN_RET_OK) {
			continue;
		}

		/* We are using 29 bit identifiers exclusively. Do not even try to parse 11 bit IDs. */
		if (msg.extid == false) {
			port->rx_errors++;
			continue;
		}

		/* Put the whole message in the queue. No oher locking required. Drop the frame
		 * if the queue is full. We don't have anything other to do with it. */
		struct nbus_switch_iq_item item = {
			port = port,
			msg = msg,
		};
		if (xQueueSend(self->iq, &item, 0) == pdTRUE) {
			port->rx_frames++;
		} else {
			port->rx_dropped++;
		}
	}
	vTaskDelete(NULL);
}


nbus_switch_ret_t nbus_switch_init(NbusSwitch *self) {
	memset(self, 0, sizeof(NbusSwitch));

	self->iq = xQueueCreate(NBUS_SWITCH_IQ_SIZE, sizeof(struct nbus_switch_iq_item));
	if (self->iq == NULL) {
		return NBUS_SWITCH_RET_FAILED;
	}

	xTaskCreate(nbus_switch_process_task, "nbus-proc", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->process_task));
	if (self->process_task == NULL) {
		return NBUS_SWITCH_RET_FAILED;
	}

	xTaskCreate(nbus_switch_housekeeping_task, "nbus-hk", configMINIMAL_STACK_SIZE + 128, (void *)self, 1, &(self->housekeeping_task));
	if (self->housekeeping_task == NULL) {
		return NBUS_SWITCH_RET_FAILED;
	}

	return NBUS_SWITCH_RET_OK;
}


nbus_switch_ret_t nbus_switch_add_port(NbusSwitch *self, Can *can) {
	if (can == NULL || can->vmt->send == NULL || can->vmt->receive == NULL) {
		return NBUS_SWITCH_RET_FAILED;
	}
	/* Add can interface to the list of ports first. */
	/** @todo locking */
	struct nbus_switch_port *port = NULL;
	for (uint32_t i = 0; i < NBUS_SWITCH_MAX_PORTS; i++) {
		if (self->ports[i].parent == NULL) {
			port = &(self->ports[i]);
			break;
		}
	}
	if (port == NULL) {
		return NBUS_SWITCH_RET_FAILED;
	}

	port->can = can;
	port->parent = self;

	xTaskCreate(nbus_switch_receive_task, "nbus-sw", configMINIMAL_STACK_SIZE + 256, (void *)port, 2, &(port->receive_task));
	if (port->receive_task == NULL) {
		port->parent = NULL;
		port->can = NULL;
		return NBUS_SWITCH_RET_FAILED;
	}

	return NBUS_SWITCH_RET_OK;
}
