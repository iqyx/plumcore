/**
 * uMeshFw network device interface
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
#include "interface_netdev.h"


int32_t interface_netdev_init(struct interface_netdev *netdev) {
	if (u_assert(netdev != NULL)) {
		return INTERFACE_NETDEV_INIT_FAILED;
	}

	memset(netdev, 0, sizeof(struct interface_netdev));
	hal_interface_init(&(netdev->descriptor), HAL_INTERFACE_TYPE_NETDEV);

	return INTERFACE_NETDEV_INIT_OK;
}


int32_t interface_netdev_free(struct interface_netdev *netdev) {
	if (u_assert(netdev != NULL)) {
		return INTERFACE_NETDEV_FREE_FAILED;
	}

	return INTERFACE_NETDEV_FREE_OK;
}


int32_t interface_netdev_send(struct interface_netdev *netdev, const uint8_t *buf, size_t len) {
	if (u_assert(netdev != NULL && buf != NULL && len > 0)) {
		return INTERFACE_NETDEV_SEND_FAILED;
	}

	if (netdev->vmt.send != NULL) {
		return netdev->vmt.send(netdev->vmt.context, buf, len);
	}

	return INTERFACE_NETDEV_SEND_FAILED;
}


int32_t interface_netdev_receive(struct interface_netdev *netdev, uint8_t *buf, size_t *len, struct interface_netdev_packet_info *info, uint32_t timeout) {
	/* info can be NULL. */
	if (u_assert(netdev != NULL && buf != NULL && len != NULL)) {
		return INTERFACE_NETDEV_RECEIVE_FAILED;
	}

	if (netdev->vmt.receive != NULL) {
		return netdev->vmt.receive(netdev->vmt.context, buf, len, info, timeout);
	}

	return INTERFACE_NETDEV_RECEIVE_FAILED;
}


/* TODO: really required? A module may expose a power management interface to handle this. */
int32_t interface_netdev_start(struct interface_netdev *netdev) {
	if (u_assert(netdev != NULL)) {
		return INTERFACE_NETDEV_START_FAILED;
	}

	if (netdev->vmt.start != NULL) {
		return netdev->vmt.start(netdev->vmt.context);
	}

	return INTERFACE_NETDEV_START_FAILED;
}


/* TODO: see above ^ */
int32_t interface_netdev_stop(struct interface_netdev *netdev) {
	if (u_assert(netdev != NULL)) {
		return INTERFACE_NETDEV_STOP_FAILED;
	}

	if (netdev->vmt.stop != NULL) {
		return netdev->vmt.stop(netdev->vmt.context);
	}

	return INTERFACE_NETDEV_STOP_FAILED;
}


uint32_t interface_netdev_get_capabilities(struct interface_netdev *netdev) {
	if (u_assert(netdev != NULL)) {
		return 0;
	}

	if (netdev->vmt.get_capabilities != NULL) {
		return netdev->vmt.get_capabilities(netdev->vmt.context);
	}

	return 0;
}


int32_t interface_netdev_get_param(struct interface_netdev *netdev, enum interface_netdev_param param, int32_t key, int32_t *value) {
	if (u_assert(netdev != NULL && value != NULL)) {
		return INTERFACE_NETDEV_GET_PARAM_FAILED;
	}

	if (netdev->vmt.get_param != NULL) {
		return netdev->vmt.get_param(netdev->vmt.context, param, key, value);
	}

	return INTERFACE_NETDEV_GET_PARAM_FAILED;
}


int32_t interface_netdev_set_param(struct interface_netdev *netdev, enum interface_netdev_param param, int32_t key, int32_t value) {
	if (u_assert(netdev != NULL)) {
		return INTERFACE_NETDEV_SET_PARAM_FAILED;
	}

	if (netdev->vmt.set_param != NULL) {
		return netdev->vmt.set_param(netdev->vmt.context, param, key, value);
	}

	return INTERFACE_NETDEV_SET_PARAM_FAILED;
}

