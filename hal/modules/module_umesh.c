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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "u_assert.h"
#include "u_log.h"
#include "hal_module.h"
#include "module_umesh.h"
#include "interface_mac.h"
#include "umesh_l2_receive.h"
#include "umesh_l3_protocol.h"
#include "umesh_l3_nbdiscovery.h"
#include "umesh_l2_status.h"

/**
 * There should be nothing more than the initialization function, free function
 * and an API to add/remove MAC interfaces.
 * All other things are handled by umesh modules internally. Dependency injection
 * of umesh module references is done in the init function.
 */

int32_t module_umesh_init(struct module_umesh *umesh, const char *name) {
	if (u_assert(umesh != NULL)) {
		return MODULE_UMESH_INIT_FAILED;
	}

	memset(umesh, 0, sizeof(struct module_umesh));
	hal_module_descriptor_init(&(umesh->module), name);
	hal_module_descriptor_set_shm(&(umesh->module), (void *)umesh, sizeof(struct module_umesh));

	/* Configure debug logging defaults. */
	umesh->l2_debug_packet_rx = false;
	umesh->l2_debug_packet_rx_error = false;
	umesh->l2_debug_packet_tx = false;
	umesh->l2_debug_packet_tx_error = false;

	/* Start uMesh components. */
	xTaskCreate(umesh_l2_receive_task, "umesh_l2_recv", configMINIMAL_STACK_SIZE + 256, (void *)umesh, 1, &(umesh->l2_receive_task));
	if (umesh_l2_pbuf_init(&(umesh->l2_pbuf)) != UMESH_L2_PBUF_INIT_OK) {
		goto err;
	}

	if (umesh_l2_nbtable_init(&(umesh->nbtable), 16) != UMESH_L2_NBTABLE_INIT_OK) {
		goto err;
	}

	if (umesh_l3_nbdiscovery_init(&(umesh->nbdiscovery), &(umesh->nbtable)) != UMESH_L3_NBDISCOVERY_INIT_OK) {
		goto err;
	}
	umesh->l3_protocols[UMESH_L3_PROTOCOL_NBDISCOVERY] = &(umesh->nbdiscovery.protocol);

	/* Initialize layer 2 key manager and its protocol. */
	/** @todo configurable key manager table size */
	if (umesh_l2_keymgr_init(&umesh->key_manager, 16) != UMESH_L2_KEYMGR_INIT_OK) {
		goto err;
	}
	umesh_l2_nbtable_set_key_manager(&umesh->nbtable, &umesh->key_manager);
	umesh->l3_protocols[UMESH_L3_PROTOCOL_KEYMGR] = &(umesh->key_manager.protocol);

	/* Initialize layer 2 file transfer protocol */
	if (umesh_l2_file_transfer_init(&umesh->file_transfer, 1) != UMESH_L2_FILE_TRANSFER_INIT_OK) {
		goto err;
	}
	umesh->l3_protocols[UMESH_L3_PROTOCOL_FILE_TRANSFER] = &(umesh->file_transfer.protocol);

	umesh_l2_status_init(&umesh->status);
	umesh->l3_protocols[UMESH_L3_PROTOCOL_STATUS] = &(umesh->status.protocol);


	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE "uMesh protocol stack initialized", umesh->module.name);
	return MODULE_UMESH_INIT_OK;
err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE "uMesh protocol stack initialization failed", umesh->module.name);
	return MODULE_UMESH_INIT_FAILED;
}


int32_t module_umesh_free(struct module_umesh *umesh) {
	if (u_assert(umesh != NULL)) {
		return MODULE_UMESH_FREE_FAILED;
	}

	umesh_l2_keymgr_free(&umesh->key_manager);
	umesh_l2_pbuf_free(&(umesh->l2_pbuf));

	return MODULE_UMESH_FREE_OK;
}


int32_t module_umesh_add_mac(struct module_umesh *umesh, struct interface_mac *mac) {
	if (u_assert(umesh != NULL && mac != NULL)) {
		return MODULE_UMESH_ADD_MAC_FAILED;
	}

	for (uint32_t i = 0; i < MODULE_UMESH_MAC_COUNT; i++) {
		if (umesh->macs[i] == NULL) {
			umesh->macs[i] = mac;
			u_log(system_log, LOG_TYPE_INFO,
				U_LOG_MODULE "uMesh stack started on the MAC '%s'",
				umesh->module.name,
				mac->descriptor.name
			);
			return MODULE_UMESH_ADD_MAC_OK;
		}
	}

	return MODULE_UMESH_ADD_MAC_FAILED;
}


int32_t module_umesh_remove_mac(struct module_umesh *umesh, struct interface_mac *mac) {
	if (u_assert(umesh != NULL && mac != NULL)) {
		return MODULE_UMESH_REMOVE_MAC_FAILED;
	}

	for (uint32_t i = 0; i < MODULE_UMESH_MAC_COUNT; i++) {
		if (umesh->macs[i] == mac) {
			umesh->macs[i] = NULL;
			u_log(system_log, LOG_TYPE_INFO,
				"%s: uMesh stack stopped on the MAC '%s'",
				umesh->module.name,
				mac->descriptor.name
			);
			return MODULE_UMESH_REMOVE_MAC_OK;
		}
	}

	return MODULE_UMESH_REMOVE_MAC_FAILED;
}


int32_t module_umesh_set_rng(struct module_umesh *umesh, struct interface_rng *rng) {
	if (u_assert(umesh != NULL && rng != NULL)) {
		return MODULE_UMESH_SET_RNG_FAILED;
	}

	umesh->rng = rng;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE "using RNG '%s'", umesh->module.name, umesh->rng->descriptor.name);

	//~ umesh_l3_ke_set_rng(&(umesh->ke), rng);
	umesh_l3_nbdiscovery_set_rng(&(umesh->nbdiscovery), rng);
	umesh_l2_keymgr_set_rng(&(umesh->key_manager), rng);
	umesh_l2_file_transfer_set_rng(&(umesh->file_transfer), rng);

	return MODULE_UMESH_SET_RNG_OK;
}


int32_t module_umesh_set_profiler(struct module_umesh *umesh, struct interface_profiling *profiler) {
	if (u_assert(umesh != NULL && profiler != NULL)) {
		return MODULE_UMESH_SET_PROFILER_FAILED;
	}

	umesh->profiler = profiler;
	//~ u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE "using time profiler '%s'", umesh->module.name, umesh->profiler->descriptor.name);

	//~ umesh_l3_ke_set_profiler(&(umesh->ke), profiler);

	return MODULE_UMESH_SET_PROFILER_OK;
}


