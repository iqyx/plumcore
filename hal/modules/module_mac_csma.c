/**
 * uMeshFw CSMA MAC module
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

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"
#include "hal_module.h"
#include "interface_netdev.h"
#include "interface_mac.h"
#include "module_mac_csma.h"


static void module_mac_csma_task(void *p) {
	struct module_mac_csma *mac = (struct module_mac_csma *)p;

	while (1) {
		struct module_mac_csma_rxpacket rxpacket;
		/* TODO: adjust the timeout properly. */
		if (interface_netdev_receive(mac->netdev, rxpacket.data, &(rxpacket.len), &(rxpacket.info), 10) == INTERFACE_NETDEV_RECEIVE_OK) {

			/* Update stats. */
			mac->stats.rx_bytes += rxpacket.len;
			mac->stats.rx_packets++;

			/* Enqueue the received packet. Do not wait if the queue is full, drop the packet instead. */
			if (xQueueSendToBack(mac->rxqueue, &rxpacket, 0) != pdTRUE) {
				/* This should not happen. */
				u_log(system_log, LOG_TYPE_WARN, "%s: reception queue full", mac->module.name);
			}
		}

		struct module_mac_csma_txpacket txpacket;
		/* Now check the transmit queue if there is something to send. */
		if (xQueueReceive(mac->txqueue, &txpacket, 0) == pdTRUE) {
			/* Update stats. */
			mac->stats.tx_bytes += txpacket.len;
			mac->stats.tx_packets++;

			if (interface_netdev_send(mac->netdev, txpacket.data, txpacket.len) != INTERFACE_NETDEV_SEND_OK) {
				//~ u_log(system_log, LOG_TYPE_WARN, "%s: send: interface not ready", mac->module.name);
			}
		}
	}
}


static int32_t module_mac_csma_send(void *context, const uint8_t *buf, size_t len) {
	if (u_assert(context != NULL && buf != NULL && len > 0)) {
		return INTERFACE_MAC_SEND_OK;
	}
	struct module_mac_csma *mac = (struct module_mac_csma *)context;

	if (len > MODULE_MAC_CSMA_PACKET_SIZE) {
		/* Packet too long. */
		return INTERFACE_MAC_SEND_FAILED;
	}

	/* TODO: avoid double copying of the data. */
	struct module_mac_csma_txpacket txpacket;
	txpacket.len = len;
	memcpy(txpacket.data, buf, len);
	if (xQueueSendToBack(mac->txqueue, &txpacket, MODULE_MAC_CSMA_TX_BLOCK_TIME) != pdTRUE) {
		/* Transmit queue is full and we cannot wait more. Return error. */
		return INTERFACE_MAC_SEND_FAILED;
	}

	return INTERFACE_MAC_SEND_OK;
}


static int32_t module_mac_csma_receive(void *context, uint8_t *buf, size_t buflen, size_t *len, struct interface_mac_packet_info *info) {
	/* info can be NULL. */
	if (u_assert(context != NULL && buf != NULL && len != NULL)) {
		return INTERFACE_MAC_RECEIVE_FAILED;
	}
	struct module_mac_csma *mac = (struct module_mac_csma *)context;

	/* TODO: avoid double copying of the data. */
	struct module_mac_csma_rxpacket rxpacket;

	if (xQueueReceive(mac->rxqueue, &rxpacket, MODULE_MAC_CSMA_RX_BLOCK_TIME) != pdTRUE) {
		*len = 0;
		return INTERFACE_MAC_RECEIVE_FAILED;
	}

	if (rxpacket.len > buflen) {
		/* Received packet too long. Return the real length. */
		*len = rxpacket.len;
		return INTERFACE_MAC_RECEIVE_FAILED;
	}

	/* The packet was dequeued correctly, return it. */
	*len = rxpacket.len;
	memcpy(buf, rxpacket.data, rxpacket.len);
	memcpy(info, &(rxpacket.info), sizeof(struct interface_mac_packet_info));

	return INTERFACE_MAC_RECEIVE_OK;
}


static int32_t module_mac_csma_start(void *context) {
	if (u_assert(context != NULL)) {
		return INTERFACE_MAC_START_FAILED;
	}
	struct module_mac_csma *mac = (struct module_mac_csma *)context;

	/* Module can be started only if the task is not created and if it is
	 * in the stopped state. */
	if (mac->state != MAC_CSMA_STATE_STOPPED || mac->task != NULL) {
		return INTERFACE_MAC_START_FAILED;
	}

	xTaskCreate(module_mac_csma_task, "csma_task", configMINIMAL_STACK_SIZE + 256, (void *)mac, 2, &(mac->task));
	if (mac->task == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, "%s: cannot create module task", mac->module.name);
		return INTERFACE_MAC_START_FAILED;
	}

	mac->state = MAC_CSMA_STATE_RUNNING;
	u_log(system_log, LOG_TYPE_INFO, "%s: task started", mac->module.name);

	return INTERFACE_MAC_START_OK;
}


static int32_t module_mac_csma_stop(void *context) {
	if (u_assert(context != NULL)) {
		return INTERFACE_MAC_STOP_FAILED;
	}
	struct module_mac_csma *mac = (struct module_mac_csma *)context;

	if (mac->state != MAC_CSMA_STATE_RUNNING || mac->task == NULL) {
		return INTERFACE_MAC_STOP_FAILED;
	}

	/* Try to stopp the task and wait some time.*/
	mac->state = MAC_CSMA_STATE_STOP_REQ;

	uint32_t i = 0;
	while (mac->state != MAC_CSMA_STATE_STOPPED) {
		if (i >= MODULE_MAC_CSMA_TASK_STOP_TIMEOUT) {
			/* Task was not stopped succesfully, apply more force. */
			u_log(system_log, LOG_TYPE_WARN, "%s: task refused to stop, killing", mac->module.name);

			vTaskDelete(mac->task);
			mac->task = NULL;
			mac->state = MAC_CSMA_STATE_STOPPED;

			return INTERFACE_MAC_STOP_OK;
		}

		vTaskDelay(10);
		i += 10;
	}

	/* Task stopped. */
	u_log(system_log, LOG_TYPE_INFO, "%s: task stopped", mac->module.name);
	mac->task = NULL;
	return INTERFACE_MAC_STOP_OK;
}


static int32_t module_mac_csma_get_statistics(void *context, struct interface_mac_statistics *statistics) {
	if (u_assert(context != NULL)) {
		return INTERFACE_MAC_GET_STATISTICS_FAILED;
	}
	struct module_mac_csma *mac = (struct module_mac_csma *)context;

	memcpy(statistics, &(mac->stats), sizeof(struct interface_mac_statistics));

	return INTERFACE_MAC_GET_STATISTICS_OK;
}


int32_t module_mac_csma_init(struct module_mac_csma *mac, const char *name, struct interface_netdev *netdev) {
	if (u_assert(mac != NULL && name != NULL && netdev != NULL)) {
		return MODULE_MAC_CSMA_INIT_FAILED;
	}

	memset(mac, 0, sizeof(struct module_mac_csma));
	hal_module_descriptor_init(&(mac->module), name);
	hal_module_descriptor_set_shm(&(mac->module), (void *)mac, sizeof(struct module_mac_csma));

	/* Initialize mac interface. */
	interface_mac_init(&(mac->iface));
	mac->iface.vmt.context = (void *)mac;
	mac->iface.vmt.send = module_mac_csma_send;
	mac->iface.vmt.receive = module_mac_csma_receive;
	mac->iface.vmt.start = module_mac_csma_start;
	mac->iface.vmt.stop = module_mac_csma_stop;
	mac->iface.vmt.get_statistics = module_mac_csma_get_statistics;

	mac->netdev = netdev;

	mac->rxqueue = xQueueCreate(MODULE_MAC_CSMA_RXQUEUE_SIZE, sizeof(struct module_mac_csma_rxpacket));
	mac->txqueue = xQueueCreate(MODULE_MAC_CSMA_TXQUEUE_SIZE, sizeof(struct module_mac_csma_txpacket));

	if (mac->rxqueue == NULL || mac->txqueue == NULL) {
		module_mac_csma_free(mac);
		u_log(system_log, LOG_TYPE_ERROR, "%s: cannot initialize MAC queues", mac->module.name);
		return MODULE_MAC_CSMA_INIT_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, "%s: CSMA MAC initialized on interface '%s'", mac->module.name, mac->netdev->descriptor.name);

	/* Now the module is initialized properly, change its state. */
	mac->state = MAC_CSMA_STATE_STOPPED;

	return MODULE_MAC_CSMA_INIT_OK;
}


int32_t module_mac_csma_free(struct module_mac_csma *mac) {
	if (u_assert(mac != NULL)) {
		return MODULE_MAC_CSMA_FREE_FAILED;
	}

	if (mac->rxqueue != NULL) {
		vQueueDelete(mac->rxqueue);
		mac->rxqueue = NULL;
	}
	if (mac->txqueue != NULL) {
		vQueueDelete(mac->txqueue);
		mac->txqueue = NULL;
	}

	return MODULE_MAC_CSMA_FREE_OK;
}
