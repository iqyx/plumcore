/**
 * uMeshFw SPI DEVICE interface
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

#ifndef _INTERFACE_SPIDEV_H_
#define _INTERFACE_SPIDEV_H_

#include <stdint.h>
#include "hal_interface.h"
#include "interface_spibus.h"

struct interface_spidev_vmt {

	int32_t (*send)(void *context, const uint8_t *txbuf, size_t txlen);
	int32_t (*receive)(void *context, uint8_t *rxbuf, size_t rxlen);
	int32_t (*exchange)(void *context, const uint8_t *txbuf, uint8_t *rxbuf, size_t len);
	int32_t (*select)(void *context);
	int32_t (*deselect)(void *context);
	void *context;
};

struct interface_spidev {
	struct hal_interface_descriptor descriptor;
	struct interface_spidev_vmt vmt;
};


int32_t interface_spidev_init(struct interface_spidev *spidev);
#define INTERFACE_SPIDEV_INIT_OK 0
#define INTERFACE_SPIDEV_INIT_FAILED -1

int32_t interface_spidev_send(struct interface_spidev *spidev, const uint8_t *txbuf, size_t txlen);
#define INTERFACE_SPIDEV_SEND_OK 0
#define INTERFACE_SPIDEV_SEND_FAILED -1

int32_t interface_spidev_receive(struct interface_spidev *spidev, uint8_t *rxbuf, size_t rxlen);
#define INTERFACE_SPIDEV_RECEIVE_OK 0
#define INTERFACE_SPIDEV_RECEIVE_FAILED -1

int32_t interface_spidev_exchange(struct interface_spidev *spidev, const uint8_t *txbuf, uint8_t *rxbuf, size_t len);
#define INTERFACE_SPIDEV_EXCHANGE_OK 0
#define INTERFACE_SPIDEV_EXCHANGE_FAILED -1

int32_t interface_spidev_select(struct interface_spidev *spidev);
#define INTERFACE_SPIDEV_SELECT_OK 0
#define INTERFACE_SPIDEV_SELECT_FAILED -1

int32_t interface_spidev_deselect(struct interface_spidev *spidev);
#define INTERFACE_SPIDEV_DESELECT_OK 0
#define INTERFACE_SPIDEV_DESELECT_FAILED -1



#endif

