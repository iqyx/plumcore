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

#ifndef _INTERFACE_NETDEV_H_
#define _INTERFACE_NETDEV_H_

#include <stdint.h>
#include "hal_interface.h"
#include "interface_spidev.h"

enum interface_netdev_capabilities {
	/* TODO: */
	INTERFACE_NETDEV_CAP_RADIO,
};

enum interface_netdev_param {
	/* TODO: */
	INTERFACE_NETDEV_PARAM_FREQ,
	INTERFACE_NETDEV_PARAM_TXPOWER,
};

struct interface_netdev_packet_info {
	int32_t fei;
	int16_t rssi;
};

struct interface_netdev_vmt {
	int32_t (*send)(void *context, const uint8_t *buf, size_t len);
	int32_t (*receive)(void *context, uint8_t *buf, size_t *len, struct interface_netdev_packet_info *info, uint32_t timeout);
	int32_t (*start)(void *context);
	int32_t (*stop)(void *context);
	uint32_t (*get_capabilities)(void *context);
	int32_t (*get_param)(void *context, enum interface_netdev_param param, int32_t key, int32_t *value);
	int32_t (*set_param)(void *context, enum interface_netdev_param param, int32_t key, int32_t value);
	void *context;
};

struct interface_netdev {
	struct hal_interface_descriptor descriptor;
	struct interface_netdev_vmt vmt;
};


int32_t interface_netdev_init(struct interface_netdev *netdev);
#define INTERFACE_NETDEV_INIT_OK 0
#define INTERFACE_NETDEV_INIT_FAILED -1

int32_t interface_netdev_free(struct interface_netdev *netdev);
#define INTERFACE_NETDEV_FREE_OK 0
#define INTERFACE_NETDEV_FREE_FAILED -1

int32_t interface_netdev_send(struct interface_netdev *netdev, const uint8_t *buf, size_t len);
#define INTERFACE_NETDEV_SEND_OK 0
#define INTERFACE_NETDEV_SEND_FAILED -1
#define INTERFACE_NETDEV_SEND_NOT_READY -1

int32_t interface_netdev_receive(struct interface_netdev *netdev, uint8_t *buf, size_t *len, struct interface_netdev_packet_info *info, uint32_t timeout);
#define INTERFACE_NETDEV_RECEIVE_OK 0
#define INTERFACE_NETDEV_RECEIVE_FAILED -1
#define INTERFACE_NETDEV_RECEIVE_TIMEOUT -2
#define INTERFACE_NETDEV_RECEIVE_NOT_READY -3

int32_t interface_netdev_start(struct interface_netdev *netdev);
#define INTERFACE_NETDEV_START_OK 0
#define INTERFACE_NETDEV_START_FAILED -1

int32_t interface_netdev_stop(struct interface_netdev *netdev);
#define INTERFACE_NETDEV_STOP_OK 0
#define INTERFACE_NETDEV_STOP_FAILED -1

uint32_t interface_netdev_get_capabilities(struct interface_netdev *netdev);

int32_t interface_netdev_get_param(struct interface_netdev *netdev, enum interface_netdev_param param, int32_t key, int32_t *value);
#define INTERFACE_NETDEV_GET_PARAM_OK 0
#define INTERFACE_NETDEV_GET_PARAM_FAILED -1
#define INTERFACE_NETDEV_GET_PARAM_UNSUPPORTED -2

int32_t interface_netdev_set_param(struct interface_netdev *netdev, enum interface_netdev_param param, int32_t key, int32_t value);
#define INTERFACE_NETDEV_SET_PARAM_OK 0
#define INTERFACE_NETDEV_SET_PARAM_FAILED -1
#define INTERFACE_NETDEV_SET_PARAM_UNSUPPORTED -2
#define INTERFACE_NETDEV_SET_PARAM_BAD_VALUE -3



#endif


