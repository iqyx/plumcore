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

#ifndef _MODULE_MAC_CSMA_H_
#define _MODULE_MAC_CSMA_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "hal_module.h"
#include "interface_netdev.h"
#include "interface_mac.h"


#define MODULE_MAC_CSMA_TASK_STOP_TIMEOUT 100
#define MODULE_MAC_CSMA_RXQUEUE_SIZE 16
#define MODULE_MAC_CSMA_TXQUEUE_SIZE 16
#define MODULE_MAC_CSMA_PACKET_SIZE 256
#define MODULE_MAC_CSMA_TX_BLOCK_TIME 100
#define MODULE_MAC_CSMA_RX_BLOCK_TIME 100

/* TODO: add timestamp */
struct module_mac_csma_rxpacket {
	uint8_t data[MODULE_MAC_CSMA_PACKET_SIZE];
	size_t len;

	struct interface_netdev_packet_info info;
};

struct module_mac_csma_txpacket {
	uint8_t data[MODULE_MAC_CSMA_PACKET_SIZE];
	size_t len;
};

enum module_mac_csma_state {
	MAC_CSMA_STATE_UNINITIALIZED = 0,
	MAC_CSMA_STATE_STOPPED,
	MAC_CSMA_STATE_RUNNING,
	MAC_CSMA_STATE_STOP_REQ,
};

struct module_mac_csma {

	struct hal_module_descriptor module;
	struct interface_mac iface;

	struct interface_netdev *netdev;

	volatile enum module_mac_csma_state state;

	/**
	 * Structure from MAC interface is used to store some statistics.
	 * It perfectly fits our needs.
	 */
	volatile struct interface_mac_statistics stats;

	/**
	 * Main task of the MAC interface. Sending and receiving is done there.
	 */
	TaskHandle_t task;

	/**
	 * Two queues are used to decouple the MAC from the rest of the system.
	 * Packets are forwarded to and from the MAC module using those.
	 */
	QueueHandle_t rxqueue;
	QueueHandle_t txqueue;
};


int32_t module_mac_csma_init(struct module_mac_csma *mac, const char *name, struct interface_netdev *netdev);
#define MODULE_MAC_CSMA_INIT_OK 0
#define MODULE_MAC_CSMA_INIT_FAILED -1

int32_t module_mac_csma_free(struct module_mac_csma *mac);
#define MODULE_MAC_CSMA_FREE_OK 0
#define MODULE_MAC_CSMA_FREE_FAILED -1


#endif

