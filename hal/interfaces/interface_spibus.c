/**
 * uMeshFw SPI bus interface
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
#include "interface_spibus.h"


int32_t interface_spibus_init(struct interface_spibus *spibus) {
	if (u_assert(spibus != NULL)) {
		return INTERFACE_SPIBUS_INIT_FAILED;
	}

	memset(spibus, 0, sizeof(struct interface_spibus));
	hal_interface_init(&(spibus->descriptor), HAL_INTERFACE_TYPE_SPIBUS);

	return INTERFACE_SPIBUS_INIT_OK;
}


int32_t interface_spibus_send(struct interface_spibus *spibus, const uint8_t *txbuf, size_t txlen) {
	if (u_assert(spibus != NULL && txbuf != NULL && txlen > 0)) {
		return INTERFACE_SPIBUS_SEND_FAILED;
	}

	if (spibus->vmt.send != NULL) {
		return spibus->vmt.send(spibus->vmt.context, txbuf, txlen);
	}

	return INTERFACE_SPIBUS_SEND_FAILED;
}


int32_t interface_spibus_receive(struct interface_spibus *spibus, uint8_t *rxbuf, size_t rxlen) {
	if (u_assert(spibus != NULL && rxbuf != NULL && rxlen > 0)) {
		return INTERFACE_SPIBUS_RECEIVE_FAILED;
	}

	if (spibus->vmt.receive != NULL) {
		return spibus->vmt.receive(spibus->vmt.context, rxbuf, rxlen);
	}

	return INTERFACE_SPIBUS_RECEIVE_FAILED;
}


int32_t interface_spibus_exchange(struct interface_spibus *spibus, const uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
	if (u_assert(spibus != NULL && txbuf != NULL && rxbuf != NULL && len > 0)) {
		return INTERFACE_SPIBUS_EXCHANGE_FAILED;
	}

	if (spibus->vmt.exchange != NULL) {
		return spibus->vmt.exchange(spibus->vmt.context, txbuf, rxbuf, len);
	}

	return INTERFACE_SPIBUS_EXCHANGE_FAILED;
}

