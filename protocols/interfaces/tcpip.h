/*
 * plumCore TCP/IP protocol interface
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
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

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "interface.h"


typedef enum {
	TCPIP_RET_OK = 0,
	TCPIP_RET_FAILED = -1,
	TCPIP_RET_DISCONNECTED = -2,
} tcpip_ret_t;






/** Socket interface */

struct tcpip_socket_vmt {
	tcpip_ret_t (*connect)(void *context, const char *address, uint16_t port);
	tcpip_ret_t (*disconnect)(void *context);
	tcpip_ret_t (*send)(void *context, const uint8_t *data, size_t len, size_t *written);

	void *context;
};

typedef struct {
	Interface interface;

	struct tcpip_socket_vmt vmt;

} ITcpIpSocket;


/** @todo listening socket interface */









/** Protocol interface */

struct tcpip_vmt {
	tcpip_ret_t (*create_client_socket)(void *context, ITcpIpSocket **socket);
	tcpip_ret_t (*release_client_socket)(void *context, ITcpIpSocket *socket);

	void *context;
};


typedef struct {
	Interface interface;

	struct tcpip_vmt vmt;

} ITcpIp;




tcpip_ret_t tcpip_init(ITcpIp *self);
tcpip_ret_t tcpip_free(ITcpIp *self);

tcpip_ret_t tcpip_create_client_socket(ITcpIp *self, ITcpIpSocket **socket);
tcpip_ret_t tcpip_release_client_socket(ITcpIp *self, ITcpIpSocket *socket);


tcpip_ret_t tcpip_socket_init(ITcpIpSocket *self);
tcpip_ret_t tcpip_socket_free(ITcpIpSocket *self);

tcpip_ret_t tcpip_socket_connect(ITcpIpSocket *self, const char *address, uint16_t port);
tcpip_ret_t tcpip_socket_disconnect(ITcpIpSocket *self);
tcpip_ret_t tcpip_socket_send(ITcpIpSocket *self, const uint8_t *data, size_t len, size_t *written);


