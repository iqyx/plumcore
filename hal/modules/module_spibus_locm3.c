/**
 * uMeshFw libopencm3 SPI bus module
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

#include <libopencm3/stm32/spi.h>
#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"
#include "interface_spibus.h"
#include "module_spibus_locm3.h"


static int32_t module_spibus_locm3_send(void *context, const uint8_t *txbuf, size_t txlen) {
	if (u_assert(context != NULL && txbuf != NULL && txlen > 0)) {
		return INTERFACE_SPIBUS_SEND_FAILED;
	}
	struct module_spibus_locm3 *spibus = (struct module_spibus_locm3 *)context;

	for (uint32_t i = 0; i < txlen; i++) {
		#if defined(STM32L4)
			spi_send8(spibus->port, txbuf[i]);
		#else
			spi_xfer(spibus->port, txbuf[i]);
		#endif
	}

	return INTERFACE_SPIBUS_SEND_OK;
}


static int32_t module_spibus_locm3_receive(void *context, uint8_t *rxbuf, size_t rxlen) {
	if (u_assert(context != NULL && rxbuf != NULL && rxlen > 0)) {
		return INTERFACE_SPIBUS_RECEIVE_FAILED;
	}
	struct module_spibus_locm3 *spibus = (struct module_spibus_locm3 *)context;

	for (uint32_t i = 0; i < rxlen; i++) {
		#if defined(STM32L4)
			spi_send8(spibus->port, 0x00);
			rxbuf[i] = spi_read8(spibus->port);
		#else
			rxbuf[i] = spi_xfer(spibus->port, 0x00);
		#endif

	}

	return INTERFACE_SPIBUS_RECEIVE_OK;
}


static int32_t module_spibus_locm3_exchange(void *context, const uint8_t *txbuf, uint8_t *rxbuf, size_t len) {
	if (u_assert(context != NULL && txbuf != NULL && rxbuf != NULL && len > 0)) {
		return INTERFACE_SPIBUS_EXCHANGE_FAILED;
	}
	struct module_spibus_locm3 *spibus = (struct module_spibus_locm3 *)context;

	for (uint32_t i = 0; i < len; i++) {
		#if defined(STM32L4)
			spi_send8(spibus->port, txbuf[i]);
			rxbuf[i] = spi_read8(spibus->port);
		#else
			rxbuf[i] = spi_xfer(spibus->port, txbuf[i]);
		#endif
	}

	return INTERFACE_SPIBUS_EXCHANGE_OK;
}


int32_t module_spibus_locm3_init(struct module_spibus_locm3 *spibus, const char *name, uint32_t port) {
	if (u_assert(spibus != NULL)) {
		return MODULE_SPIBUS_LOCM3_INIT_FAILED;
	}
	(void)name;

	memset(spibus, 0, sizeof(struct module_spibus_locm3));

	/* Initialize spibus interface. */
	interface_spibus_init(&(spibus->iface));
	spibus->iface.vmt.context = (void *)spibus;
	spibus->iface.vmt.send = module_spibus_locm3_send;
	spibus->iface.vmt.receive = module_spibus_locm3_receive;
	spibus->iface.vmt.exchange = module_spibus_locm3_exchange;

	spibus->port = port;

	/* Defaults only, can be changed using the spibus API. */
	spi_set_master_mode(spibus->port);
	spi_set_baudrate_prescaler(spibus->port, SPI_CR1_BR_FPCLK_DIV_2);
	spi_set_clock_polarity_0(spibus->port);
	spi_set_clock_phase_0(spibus->port);
	spi_set_full_duplex_mode(spibus->port);
	spi_set_unidirectional_mode(spibus->port);
	spi_enable_software_slave_management(spibus->port);
	spi_send_msb_first(spibus->port);
	spi_set_nss_high(spibus->port);
	#if defined(STM32L4)
		spi_set_data_size(spibus->port, SPI_CR2_DS_8BIT);
	#endif
	spi_enable(spibus->port);

	u_log(system_log, LOG_TYPE_INFO, "module SPI bus initialized using default settings");

	return MODULE_SPIBUS_LOCM3_INIT_OK;
}

