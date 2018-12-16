/**
 * uMeshFw SPI device module, libopencm3 GPIO chip selects
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

#include <libopencm3/stm32/gpio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "interface_spibus.h"
#include "interface_spidev.h"
#include "module_spidev_locm3.h"


static int32_t module_spidev_locm3_send(void *context, const uint8_t *txbuf, size_t txlen) {
	if (u_assert(context != NULL && txbuf != NULL && txlen > 0)) {
		return INTERFACE_SPIDEV_SEND_FAILED;
	}

	struct module_spidev_locm3 *spidev = (struct module_spidev_locm3 *)context;
	return interface_spibus_send(spidev->spibus, txbuf, txlen);
}


static int32_t module_spidev_locm3_receive(void *context, uint8_t *rxbuf, size_t rxlen) {
	if (u_assert(context != NULL && rxbuf != NULL && rxlen > 0)) {
		return INTERFACE_SPIDEV_RECEIVE_FAILED;
	}
	struct module_spidev_locm3 *spidev = (struct module_spidev_locm3 *)context;
	return interface_spibus_receive(spidev->spibus, rxbuf, rxlen);
}


static int32_t module_spidev_locm3_exchange(void *context, const uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
	if (u_assert(context != NULL && txbuf != NULL && rxbuf != NULL && len > 0)) {
		return INTERFACE_SPIDEV_EXCHANGE_FAILED;
	}
	struct module_spidev_locm3 *spidev = (struct module_spidev_locm3 *)context;
	return interface_spibus_exchange(spidev->spibus, txbuf, rxbuf, len);
}


static int32_t module_spidev_locm3_select(void *context) {
	if (u_assert(context != NULL)) {
		return INTERFACE_SPIDEV_SELECT_FAILED;
	}

	struct module_spidev_locm3 *spidev = (struct module_spidev_locm3 *)context;
	gpio_clear(spidev->cs_port, spidev->cs_pin);

	return INTERFACE_SPIDEV_SELECT_OK;
}


static int32_t module_spidev_locm3_deselect(void *context) {
	if (u_assert(context != NULL)) {
		return INTERFACE_SPIDEV_DESELECT_FAILED;
	}

	struct module_spidev_locm3 *spidev = (struct module_spidev_locm3 *)context;
	gpio_set(spidev->cs_port, spidev->cs_pin);

	return INTERFACE_SPIDEV_DESELECT_OK;
}


int32_t module_spidev_locm3_init(struct module_spidev_locm3 *spidev, const char *name, struct interface_spibus *spibus, uint32_t cs_port, uint32_t cs_pin) {
	if (u_assert(spidev != NULL && spibus != NULL)) {
		return MODULE_SPIDEV_LOCM3_INIT_FAILED;
	}
	(void)name;

	memset(spidev, 0, sizeof(struct module_spidev_locm3));

	/* Initialize spidev interface. */
	interface_spidev_init(&(spidev->iface));
	spidev->iface.vmt.context = (void *)spidev;
	spidev->iface.vmt.send = module_spidev_locm3_send;
	spidev->iface.vmt.receive = module_spidev_locm3_receive;
	spidev->iface.vmt.exchange = module_spidev_locm3_exchange;
	spidev->iface.vmt.select = module_spidev_locm3_select;
	spidev->iface.vmt.deselect = module_spidev_locm3_deselect;

	spidev->cs_port = cs_port;
	spidev->cs_pin = cs_pin;
	spidev->spibus = spibus;
	// module_spidev_locm3_deselect((void *)spidev);

	u_log(system_log, LOG_TYPE_INFO, "SPI device initialized on bus %s", spidev->spibus->descriptor.name);

	return MODULE_SPIDEV_LOCM3_INIT_OK;
}


int32_t module_spidev_locm3_free(struct module_spidev_locm3 *spidev) {
	if (u_assert(spidev != NULL)) {
		return MODULE_SPIDEV_LOCM3_FREE_FAILED;
	}

	return MODULE_SPIDEV_LOCM3_FREE_OK;
}
