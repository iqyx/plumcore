/**
 * uMeshFw HAL MAC interface
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

#include "u_assert.h"
#include "hal_interface.h"
#include "interface_mac.h"


int32_t interface_mac_init(struct interface_mac *mac) {
	if (u_assert(mac != NULL)) {
		return INTERFACE_MAC_INIT_FAILED;
	}

	memset(mac, 0, sizeof(struct interface_mac));
	hal_interface_init(&(mac->descriptor), HAL_INTERFACE_TYPE_MAC);

	return INTERFACE_MAC_INIT_OK;
}


int32_t interface_mac_free(struct interface_mac *mac) {
	if (u_assert(mac != NULL)) {
		return INTERFACE_MAC_FREE_FAILED;
	}

	return INTERFACE_MAC_FREE_OK;
}


int32_t interface_mac_send(struct interface_mac *mac, const uint8_t *buf, size_t len) {
	if (u_assert(mac != NULL && buf != NULL && len > 0)) {
		return INTERFACE_MAC_SEND_FAILED;
	}

	if (mac->vmt.send != NULL) {
		return mac->vmt.send(mac->vmt.context, buf, len);
	}

	return INTERFACE_MAC_SEND_FAILED;
}


int32_t interface_mac_receive(struct interface_mac *mac, uint8_t *buf, size_t buflen, size_t *len, struct interface_mac_packet_info *info) {
	/* info can be NULL. */
	if (u_assert(mac != NULL && buf != NULL && len != NULL)) {
		return INTERFACE_MAC_RECEIVE_FAILED;
	}

	if (mac->vmt.receive != NULL) {
		return mac->vmt.receive(mac->vmt.context, buf, buflen, len, info);
	}

	return INTERFACE_MAC_RECEIVE_FAILED;
}


int32_t interface_mac_start(struct interface_mac *mac) {
	if (u_assert(mac != NULL)) {
		return INTERFACE_MAC_START_FAILED;
	}

	if (mac->vmt.start != NULL) {
		return mac->vmt.start(mac->vmt.context);
	}

	return INTERFACE_MAC_START_FAILED;
}


int32_t interface_mac_stop(struct interface_mac *mac) {
	if (u_assert(mac != NULL)) {
		return INTERFACE_MAC_STOP_FAILED;
	}

	if (mac->vmt.stop != NULL) {
		return mac->vmt.stop(mac->vmt.context);
	}

	return INTERFACE_MAC_STOP_FAILED;
}


int32_t interface_mac_get_statistics(struct interface_mac *mac, struct interface_mac_statistics *statistics) {
	if (u_assert(mac != NULL && statistics != NULL)) {
		return INTERFACE_MAC_GET_STATISTICS_FAILED;
	}

	if (mac->vmt.get_statistics != NULL) {
		return mac->vmt.get_statistics(mac->vmt.context, statistics);
	}

	return INTERFACE_MAC_GET_STATISTICS_FAILED;
}



