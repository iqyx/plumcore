/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * OnSemi NCN26010 10Base-T1S ethernet MAC/PHY driver service
 *
 * Register defines and register access routines borrowed
 * from the CycloneTCP project.
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * Copyright (C) 2010-2025, Oryx Embedded SARL (www.oryx-embedded.com)
 *
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/spi.h>
#include <interfaces/datagram.h>

#include "ncn26010.h"

#define MODULE_NAME "ncn26010"


static uint32_t parity(uint32_t data) {
	data ^= data >> 1;
	data ^= data >> 2;
	data ^= data >> 4;
	data ^= data >> 8;
	data ^= data >> 16;

	/* Return '1' when the number of bits set to one in the supplied bit
	 * stream is even (resulting in an odd number of ones when the parity is
	 * included), otherwise return '0'. */
	return ~data & 0x01;
}


static ncn26010_ret_t ncn26010_read(Ncn26010 *self, uint8_t mms, uint16_t addr, uint32_t *data) {

	uint32_t header = NCN26010_CTRL_HEADER_AID;
	header |= (mms  << NCN26010_CTRL_HEADER_MMS_SHIFT ) & NCN26010_CTRL_HEADER_MMS_MASK;
	header |= (addr << NCN26010_CTRL_HEADER_ADDR_SHIFT) & NCN26010_CTRL_HEADER_ADDR_MASK;
	if (parity(header) != 0) {
		header |= NCN26010_CTRL_HEADER_P;
	}

	uint8_t txbuf[4] = {
		(header >> 24) & 0xff,
		(header >> 16) & 0xff,
		(header >> 8) & 0xff,
		header & 0xff
	};

	uint8_t rxbuf[8] = {0};

	self->spi->vmt->select(self->spi);
	self->spi->vmt->send(self->spi, txbuf, sizeof(txbuf));
	self->spi->vmt->receive(self->spi, rxbuf, sizeof(rxbuf));
	self->spi->vmt->deselect(self->spi);


	if (data != NULL && !memcmp(txbuf, rxbuf, 4)) {
		*data = rxbuf[4] << 24 | rxbuf[5] << 16 | rxbuf[6] << 8 | rxbuf[7];

		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("read reg.%u[0x%04x] = 0x%08x"), mms, addr, *data);
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("txbuf = 0x%02x, 0x%02x, 0x%02x, 0x%02x"),
			//txbuf[0], txbuf[1], txbuf[2], txbuf[3]);
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("rxbuf = 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x"),
			//rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3], rxbuf[4], rxbuf[5], rxbuf[6], rxbuf[7]);

		return NCN26010_RET_OK;
	}

	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("register read failed"));
	return NCN26010_RET_FAILED;
}


static ncn26010_ret_t ncn26010_write(Ncn26010 *self, uint8_t mms, uint16_t addr, uint32_t data) {

	uint32_t header = NCN26010_CTRL_HEADER_WNR | NCN26010_CTRL_HEADER_AID;
	header |= (mms  << NCN26010_CTRL_HEADER_MMS_SHIFT ) & NCN26010_CTRL_HEADER_MMS_MASK;
	header |= (addr << NCN26010_CTRL_HEADER_ADDR_SHIFT) & NCN26010_CTRL_HEADER_ADDR_MASK;
	if (parity(header) != 0) {
		header |= NCN26010_CTRL_HEADER_P;
	}

	uint8_t txbuf[8] = {
		(header >> 24) & 0xff,
		(header >> 16) & 0xff,
		(header >> 8) & 0xff,
		header & 0xff,
		(data >> 24) & 0xff,
		(data >> 16) & 0xff,
		(data >> 8) & 0xff,
		data & 0xff
	};
	uint8_t rxbuf[4] = {0};

	self->spi->vmt->select(self->spi);
	self->spi->vmt->send(self->spi, txbuf, sizeof(txbuf));
	self->spi->vmt->receive(self->spi, rxbuf, sizeof(rxbuf));
	self->spi->vmt->deselect(self->spi);

	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("write reg.%u[0x%04x] = 0x%08x"), mms, addr, data);
	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("txbuf = 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x"),
		//txbuf[0], txbuf[1], txbuf[2], txbuf[3], txbuf[4], txbuf[5], txbuf[6], txbuf[7]);
	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("rxbuf = 0x%02x, 0x%02x, 0x%02x, 0x%02x"),
		//rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3]);

	uint32_t status = 0;
	ncn26010_read(self, NCN26010_STATUS0, &status);

	if (!memcmp(txbuf + 4, rxbuf, 4)) {
		return NCN26010_RET_OK;
	}

	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("register write failed"));
	return NCN26010_RET_FAILED;
}


static ncn26010_ret_t ncn26010_rmw_set(Ncn26010 *self, uint8_t mms, uint16_t addr, uint32_t data) {
	uint32_t val = 0;
	if (ncn26010_read(self, mms, addr, &val) != NCN26010_RET_OK) {
		return NCN26010_RET_FAILED;
	}
	val |= data;
	if (ncn26010_write(self, mms, addr, val) != NCN26010_RET_OK) {
		return NCN26010_RET_FAILED;
	}

	return NCN26010_RET_OK;
}


static ncn26010_ret_t reset(Ncn26010 *self) {
	ncn26010_rmw_set(self, NCN26010_RESET, NCN26010_RESET_RESET);
	for (uint32_t attempts = 0; ; attempts++) {
		uint32_t val = 0;
		ncn26010_read(self, NCN26010_RESET, &val);

		if (!(val & NCN26010_RESET_RESET)) {
			break;
		}
		if (attempts > NCN26010_RESET_ATTEMPTS) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("not responding to a reset request"));
			return NCN26010_RET_FAILED;
		}

		vTaskDelay(1);
	}

	/* Check the RESETC bit and clear it. */
	for (uint32_t attempts = 0; ; attempts++) {
		uint32_t val = 0;
		ncn26010_read(self, NCN26010_STATUS0, &val);

		if (val & NCN26010_STATUS0_RESETC) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("reset ok"));
			break;
		}
		if (attempts > NCN26010_RESET_ATTEMPTS) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("not responding to a reset request: RESETC not set"));
			return NCN26010_RET_FAILED;
		}

		vTaskDelay(1);
	}

	/* For some reason, HDRE flag (header invalid) is set from the very beginning.
	 * Clear it immediately after reset. */
	ncn26010_write(self, NCN26010_STATUS0, NCN26010_STATUS0_RESETC | NCN26010_STATUS0_HDRE);

	return NCN26010_RET_OK;
}


ncn26010_ret_t ncn26010_init(Ncn26010 *self, SpiDev *spi) {
	memset(self, 0, sizeof(Ncn26010));
	self->spi = spi;

	/* Check major/minor version, model number, chip revision number. */
	uint32_t idver = 0;
	if (ncn26010_read(self, 0, 0, &idver) != NCN26010_RET_OK) {
		goto err;
	}
	self->major = (idver >> 4) & 0xf;
	self->minor = idver & 0xf;

	uint32_t phyid = 0;
	if (ncn26010_read(self, 0, 1, &phyid) != NCN26010_RET_OK) {
		goto err;
	}
	self->model = (phyid >> 4) & 0x1a;
	self->revision = phyid & 0xf;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("found model = 0x%02x, revision = %u, version = %u.%u"),
		self->model,
		self->revision,
		self->major,
		self->minor
	);

	/* Reset the device before reconfiguring it. */
	if (reset(self) != NCN26010_RET_OK) {
		goto err;
	}

	/** @todo get/compute MAC address here */
	self->mac_addr[0] = 0x02;
	self->mac_addr[0] = 0x12;
	self->mac_addr[0] = 0x23;
	self->mac_addr[0] = 0x34;
	self->mac_addr[0] = 0x45;
	self->mac_addr[0] = 0x56;

	/* SPI protocol engine setup. */
	ncn26010_write(self, NCN26010_CONFIG0,
		NCN26010_CONFIG0_CSARFE |
		NCN26010_CONFIG0_ZARFE |
		NCN26010_CONFIG0_TXCTHRESH_16_CREDITS |
		NCN26010_CONFIG0_CPS_64_BYTES |
		NCN26010_CONFIG0_SYNC
	);

	/* PHY setup */
	ncn26010_write(self, NCN26010_PHYCTRL, NCN26010_PHYCTRL_LINK_CONTROL);
	ncn26010_write(self, NCN26010_PLCACTRL0, 0);

	/* MAC setup */
	ncn26010_write(self, NCN26010_MACCTRL0,
		NCN26010_MACCTRL0_TXEN |
		NCN26010_MACCTRL0_RXEN |
		NCN26010_MACCTRL0_FCSA
	);

	/* Enable MAC filtering. */
	//ncn26010_write(self, NCN26010_ADDRFILT0L, 0x55555555);
	//ncn26010_write(self, NCN26010_ADDRFILT0H, 0x80006666);

	//ncn26010_rmw_set(self, NCN26010_MACCTRL0, NCN26010_MACCTRL0_ADRF);
	//ncn26010_rmw_set(self, NCN26010_MACCTRL0, NCN26010_MACCTRL0_MCSF);
	//ncn26010_rmw_set(self, NCN26010_MACCTRL0, NCN26010_MACCTRL0_BCSF);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("802.3cg 10Base-T1S MAC/PHY initialized"));
	return NCN26010_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("init failed"));
	return NCN26010_RET_FAILED;
}


ncn26010_ret_t ncn26010_free(Ncn26010 *self) {

	return NCN26010_RET_OK;
}


ncn26010_ret_t ncn26010_irq_handler(Ncn26010 *self) {

	return NCN26010_RET_OK;
}


ncn26010_ret_t ncn26010_link_status(Ncn26010 *self, bool *up, bool *neg_completed) {
	uint32_t val = 0;
	if (ncn26010_read(self, NCN26010_PHYSTATUS, &val) != NCN26010_RET_OK) {
		return NCN26010_RET_FAILED;
	}
	if (up != NULL) {
		*up = val & NCN26010_PHYSTATUS_LINK_STATUS;
	}
	if (neg_completed != NULL) {
		*neg_completed = val & NCN26010_PHYSTATUS_LINK_NEGOTIATION_COMPLETE;
	}

	return NCN26010_RET_OK;
}


ncn26010_ret_t ncn26010_sleep(Ncn26010 *self) {
	/* Put the 10Base-T1S PHY PMA into its low power mode to cut the idle current consumption. */
	if (ncn26010_rmw_set(self, NCN26010_T1SPMACTRL, NCN26010_T1SPMACTRL_LOW_POWER_MODE) != NCN26010_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot enter low power mode"));
		return NCN26010_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("entered low power mode"));
	return NCN26010_RET_OK;
}


ncn26010_ret_t ncn26010_send(Ncn26010 *self, const uint8_t *buf, size_t len) {
	uint32_t status = 0;
	ncn26010_read(self, NCN26010_BUFSTS, &status);
	uint32_t chunks_available = (status & NCN26010_BUFSTS_TXC_MASK) >> NCN26010_BUFSTS_TXC_SHIFT;

	if (len > (chunks_available * 64)) {
		return NCN26010_RET_FAILED;
	}

	for (size_t chunk = 0; len > 0; chunk++) {
		size_t chunk_size = len >= 64 ? 64 : len;
		len -= chunk_size;

		uint32_t header = NCN26010_TX_HEADER_DNC | NCN26010_TX_HEADER_NORX | NCN26010_TX_HEADER_DV;
		//uint32_t header = NCN26010_TX_HEADER_DNC | NCN26010_TX_HEADER_DV;
		/* Start of the packet. */
		if (chunk == 0) {
			header |= NCN26010_TX_HEADER_SV;
		}
		/* End of the packet. */
		if (len == 0) {
			header |= NCN26010_TX_HEADER_EV;
			header |= ((chunk_size - 1) << NCN26010_TX_HEADER_EBO_SHIFT) & NCN26010_TX_HEADER_EBO_MASK;
		}
		if (parity(header) != 0) {
			header |= NCN26010_CTRL_HEADER_P;
		}

		uint8_t txbuf[68] = {0};
		txbuf[0] = (header >> 24) & 0xff;
		txbuf[1] = (header >> 16) & 0xff;
		txbuf[2] = (header >> 8) & 0xff;
		txbuf[3] = header & 0xff;
		memcpy(txbuf + 4, buf, chunk_size);

		self->spi->vmt->select(self->spi);
		self->spi->vmt->send(self->spi, txbuf, sizeof(txbuf));
		self->spi->vmt->deselect(self->spi);

		buf += chunk_size;
		/* len is already decremented. */
	}

	//uint32_t val = 0;
	//ncn26010_read(self, NCN26010_STATUS0, &val);
	//ncn26010_read(self, NCN26010_T1SPCSSTATUS, &val);
	//ncn26010_read(self, NCN26010_T1SPCSPHYCOL, &val);

	return NCN26010_RET_OK;
}


ncn26010_ret_t ncn26010_recv(Ncn26010 *self, uint8_t *buf, size_t size, size_t *len) {
	if (len == NULL) {
		return NCN26010_RET_FAILED;
	}
	*len = 0;

	/* Accept chunks if there is buffer space available for at least one additional chunk. */
	for (size_t chunk = 0; size >= 64; chunk++) {
		uint32_t header = NCN26010_TX_HEADER_DNC;
		if (parity(header) != 0) {
			header |= NCN26010_CTRL_HEADER_P;
		}

		uint8_t txbuf[68] = {0};
		txbuf[0] = (header >> 24) & 0xff;
		txbuf[1] = (header >> 16) & 0xff;
		txbuf[2] = (header >> 8) & 0xff;
		txbuf[3] = header & 0xff;

		uint8_t rxbuf[68] = {0};

		self->spi->vmt->select(self->spi);
		self->spi->vmt->exchange(self->spi, txbuf, rxbuf, sizeof(txbuf));
		self->spi->vmt->deselect(self->spi);

		size_t chunk_size = 64;
		uint32_t footer = rxbuf[64] << 24 | rxbuf[65] << 16 | rxbuf[66] << 8 | rxbuf[67];
		//u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("footer = 0x%08x"), footer);

		if (!(footer & NCN26010_RX_FOOTER_DV)) {
			return NCN26010_RET_NOOP;
		}

		/* First chunk must have its SV bit set, others must not. */
		if ((chunk == 0) == !(footer & NCN26010_RX_FOOTER_SV)) {
			return NCN26010_RET_FAILED;
		}

		if (footer & NCN26010_RX_FOOTER_EV) {
			/* End of frame. Crop chunk_size to the current chunk size. */
			chunk_size = ((footer & NCN26010_RX_FOOTER_EBO_MASK) >> NCN26010_RX_FOOTER_EBO_SHIFT) + 1;
		}

		memcpy(buf, rxbuf, chunk_size);
		buf += chunk_size;
		size -= chunk_size;
		(*len) += chunk_size;

		if (footer & NCN26010_RX_FOOTER_EV) {
			return NCN26010_RET_OK;
		}
	};

	/* Frame did not fit. */
	return NCN26010_RET_FAILED;
}


