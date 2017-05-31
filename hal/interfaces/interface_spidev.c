/**
 * uMeshFw SPI device interface
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
#include "interface_spidev.h"


int32_t interface_spidev_init(struct interface_spidev *spidev) {
	if (u_assert(spidev != NULL)) {
		return INTERFACE_SPIDEV_INIT_FAILED;
	}

	memset(spidev, 0, sizeof(struct interface_spidev));
	hal_interface_init(&(spidev->descriptor), HAL_INTERFACE_TYPE_SPIDEV);

	return INTERFACE_SPIDEV_INIT_OK;
}


int32_t interface_spidev_send(struct interface_spidev *spidev, const uint8_t *txbuf, size_t txlen) {
	if (u_assert(spidev != NULL && txbuf != NULL && txlen > 0)) {
		return INTERFACE_SPIDEV_SEND_FAILED;
	}

	if (spidev->vmt.send != NULL) {
		return spidev->vmt.send(spidev->vmt.context, txbuf, txlen);
	}

	return INTERFACE_SPIDEV_SEND_FAILED;
}


int32_t interface_spidev_receive(struct interface_spidev *spidev, uint8_t *rxbuf, size_t rxlen) {
	if (u_assert(spidev != NULL && rxbuf != NULL && rxlen > 0)) {
		return INTERFACE_SPIDEV_RECEIVE_FAILED;
	}

	if (spidev->vmt.receive != NULL) {
		return spidev->vmt.receive(spidev->vmt.context, rxbuf, rxlen);
	}

	return INTERFACE_SPIDEV_RECEIVE_FAILED;
}


int32_t interface_spidev_exchange(struct interface_spidev *spidev, const uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
	if (u_assert(spidev != NULL && txbuf != NULL && rxbuf != NULL && len > 0)) {
		return INTERFACE_SPIDEV_EXCHANGE_FAILED;
	}

	if (spidev->vmt.exchange != NULL) {
		return spidev->vmt.exchange(spidev->vmt.context, txbuf, rxbuf, len);
	}

	return INTERFACE_SPIDEV_EXCHANGE_FAILED;
}


int32_t interface_spidev_select(struct interface_spidev *spidev) {
	if (u_assert(spidev != NULL)) {
		return INTERFACE_SPIDEV_SELECT_FAILED;
	}

	if (spidev->vmt.select != NULL) {
		return spidev->vmt.select(spidev->vmt.context);
	}

	return INTERFACE_SPIDEV_SELECT_FAILED;
}


int32_t interface_spidev_deselect(struct interface_spidev *spidev) {
	if (u_assert(spidev != NULL)) {
		return INTERFACE_SPIDEV_DESELECT_FAILED;
	}

	if (spidev->vmt.deselect != NULL) {
		return spidev->vmt.deselect(spidev->vmt.context);
	}

	return INTERFACE_SPIDEV_DESELECT_FAILED;
}

