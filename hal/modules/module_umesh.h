/**
 * uMeshFw uMesh protocol stack module
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

#ifndef _MODULE_UMESH_H_
#define _MODULE_UMESH_H_

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "hal_module.h"
#include "interface_mac.h"
#include "umesh_l2_pbuf.h"
#include "umesh_l2_nbtable.h"
#include "umesh_l3_nbdiscovery.h"
#include "interface_rng.h"
#include "umesh_l2_file_transfer.h"
#include "umesh_l2_status.h"


#define MODULE_UMESH_MAC_COUNT 2

/**
 * All uMesh related variables and structures are strictly contained within
 * this single umesh structure (an uMesh instance context).
 * This allows us (among the other things) to run multiple uMesh stacks
 * simultaneously with totally independent internal states.
 */
struct module_umesh {

	struct hal_module_descriptor module;

	/* An array of all added MACs. */
	struct interface_mac *macs[MODULE_UMESH_MAC_COUNT];
	struct interface_rng *rng;
	struct interface_profiling *profiler;

	TaskHandle_t l2_receive_task;

	/* Packet buffers. As the packet reception is serialized, only one
	 * L2 and L3 packet buffer is needed. */
	struct umesh_l2_pbuf l2_pbuf;

	/* Layer 2 packet statistics. */
	uint32_t l2_packets_rx, l2_packets_rx_dropped;
	uint32_t l2_packets_tx, l2_packets_tx_dropped;

	uint32_t l2_data_rx, l2_data_rx_dropped;
	uint32_t l2_data_tx, l2_data_tx_dropped;

	/* Layer 2 debug control. */
	bool l2_debug_packet_tx;
	bool l2_debug_packet_tx_error;

	bool l2_debug_packet_rx;
	bool l2_debug_packet_rx_error;

	/** @todo list of layer 3 protocols with their interfaces */
	struct umesh_l3_protocol *l3_protocols[UMESH_L3_PROTOCOL_COUNT];

	struct umesh_l2_nbtable nbtable;
	struct umesh_l3_nbdiscovery nbdiscovery;
	L2KeyManager key_manager;
	L2FileTransfer file_transfer;
	UmeshL2Status status;
};




int32_t module_umesh_init(struct module_umesh *umesh, const char *name);
#define MODULE_UMESH_INIT_OK 0
#define MODULE_UMESH_INIT_FAILED -1

int32_t module_umesh_free(struct module_umesh *umesh);
#define MODULE_UMESH_FREE_OK 0
#define MODULE_UMESH_FREE_FAILED -1

int32_t module_umesh_add_mac(struct module_umesh *umesh, struct interface_mac *mac);
#define MODULE_UMESH_ADD_MAC_OK 0
#define MODULE_UMESH_ADD_MAC_FAILED -1

int32_t module_umesh_remove_mac(struct module_umesh *umesh, struct interface_mac *mac);
#define MODULE_UMESH_REMOVE_MAC_OK 0
#define MODULE_UMESH_REMOVE_MAC_FAILED -1

int32_t module_umesh_set_rng(struct module_umesh *umesh, struct interface_rng *rng);
#define MODULE_UMESH_SET_RNG_OK 0
#define MODULE_UMESH_SET_RNG_FAILED -1

int32_t module_umesh_set_profiler(struct module_umesh *umesh, struct interface_profiling *profiler);
#define MODULE_UMESH_SET_PROFILER_OK 0
#define MODULE_UMESH_SET_PROFILER_FAILED -1

#endif
