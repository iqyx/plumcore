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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "u_assert.h"
#include "interface.h"

#include "tcpip.h"

tcpip_ret_t tcpip_init(ITcpIp *self) {
	if (u_assert(self != NULL)) {
		return TCPIP_RET_FAILED;
	}

	memset(self, 0, sizeof(ITcpIp));
	uhal_interface_init(&self->interface);

	return TCPIP_RET_OK;
}


tcpip_ret_t tcpip_free(ITcpIp *self) {
	if (u_assert(self != NULL)) {
		return TCPIP_RET_FAILED;
	}

	/* Nothing to free yet. */

	return TCPIP_RET_OK;
}


tcpip_ret_t tcpip_create_client_socket(ITcpIp *self, ITcpIpSocket **socket) {
	if (u_assert(self != NULL) ||
	    u_assert(socket != NULL)) {
		return TCPIP_RET_FAILED;
	}

	if (self->vmt.create_client_socket != NULL) {
		return self->vmt.create_client_socket(self->vmt.context, socket);
	}

	return TCPIP_RET_OK;
}


tcpip_ret_t tcpip_release_client_socket(ITcpIp *self, ITcpIpSocket *socket) {
	if (u_assert(self != NULL) ||
	    u_assert(socket != NULL)) {
		return TCPIP_RET_FAILED;
	}

	if (self->vmt.release_client_socket != NULL) {
		return self->vmt.release_client_socket(self->vmt.context, socket);
	}

	return TCPIP_RET_OK;
}



tcpip_ret_t tcpip_socket_init(ITcpIpSocket *self) {
	if (u_assert(self != NULL)) {
		return TCPIP_RET_FAILED;
	}

	memset(self, 0, sizeof(ITcpIpSocket));
	uhal_interface_init(&self->interface);

	return TCPIP_RET_OK;
}


tcpip_ret_t tcpip_socket_free(ITcpIpSocket *self) {
	if (u_assert(self != NULL)) {
		return TCPIP_RET_FAILED;
	}

	return TCPIP_RET_OK;
}


tcpip_ret_t tcpip_socket_connect(ITcpIpSocket *self, const char *address, uint16_t port) {
	if (u_assert(self != NULL) ||
	    u_assert(port != NULL)) {
		return TCPIP_RET_FAILED;
	}

	if (self->vmt.connect != NULL) {
		return self->vmt.connect(self->vmt.context, address, port);
	}

	return TCPIP_RET_OK;
}


tcpip_ret_t tcpip_socket_disconnect(ITcpIpSocket *self) {
	if (u_assert(self != NULL)) {
		return TCPIP_RET_FAILED;
	}

	if (self->vmt.disconnect != NULL) {
		return self->vmt.disconnect(self->vmt.context);
	}

	return TCPIP_RET_OK;
}


tcpip_ret_t tcpip_socket_send(ITcpIpSocket *self, const uint8_t *data, size_t len, size_t *written) {
	if (u_assert(self != NULL) ||
	    u_assert(data != NULL) ||
	    u_assert(len > 0) ||
	    u_assert(written != NULL)) {
		return TCPIP_RET_FAILED;
	}

	if (self->vmt.send != NULL) {
		return self->vmt.send(self->vmt.context, data, len, written);
	}

	return TCPIP_RET_OK;
}
