/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Microchip MCP3561/2/4 ADC driver
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>

#include "mcp3564.h"

#define MODULE_NAME "mcp3564"


mcp3564_ret_t mcp3564_init(Mcp3564 *self, SpiDev *spidev) {
	memset(self, 0, sizeof(Mcp3564));

	self->spidev = spidev;

	/* Reset the chip and initialise with defaults. */
	// mcp3564_send_cmd(self, MCP3564_CMD_FULL_RESET, NULL, NULL, 0, NULL, 0);
	// vTaskDelay(5);

	uint32_t pn = 0;
	mcp3564_read_reg(self, MCP3564_REG_RESERVED_PN, 2, &pn, NULL);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("product number = 0x%04x"), pn);

	memcpy(self->regs, MCP3564_REG_DEFAULTS, sizeof(MCP3564_REG_DEFAULTS));

	/* Register values overriden for test only */
	self->regs[MCP3564_REG_CONFIG0] = 0x22;
	// self->regs[MCP3564_REG_CONFIG1] = 0x30;
	self->regs[MCP3564_REG_CONFIG2] = 0xbb;
	self->regs[MCP3564_REG_CONFIG3] = 0xa0;

	mcp3564_update(self);

	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_send_cmd(Mcp3564 *self, enum mcp3564_command cmd, uint8_t *status, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen) {
	self->spidev->vmt->select(self->spidev);

	/* Send the command and read the status byte. 0x40 is the default device address shifted 6 bits left. */
	uint8_t txbuf[1] = {cmd | 0x40};
	uint8_t rxbuf[1] = {0};
	self->spidev->vmt->exchange(self->spidev, txbuf, rxbuf, 1);

	if (status != NULL) {
		*status = rxbuf[0];
	}
	if (txdata != NULL) {
		self->spidev->vmt->send(self->spidev, txdata, txlen);
	}
	if (rxdata != NULL) {
		self->spidev->vmt->receive(self->spidev, rxdata, rxlen);
	}

	self->spidev->vmt->deselect(self->spidev);

	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_read_reg(Mcp3564 *self, enum mcp3564_reg reg, size_t bytes, uint32_t *value, uint8_t *status) {
	if (bytes < 1 || bytes > 4) {
		return MCP3564_RET_FAILED;
	}
	uint8_t b[4] = {0};

	if (mcp3564_send_cmd(self, MCP3564_CMD_READ | (reg << 2), status, NULL, 0, b, bytes) != MCP3564_RET_OK) {
		return MCP3564_RET_FAILED;
	}
	uint32_t v = 0;
	for (size_t i = 0; i < bytes; i++) {
		v = (v << 8) | b[i];
	}
	if (value != NULL) {
		*value = v;
	}

	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_write_reg(Mcp3564 *self, enum mcp3564_reg reg, size_t bytes, uint32_t value, uint8_t *status) {
	if (bytes < 1 || bytes > 4) {
		return MCP3564_RET_FAILED;
	}
	uint8_t b[4] = {0};
	for (size_t i = 0; i < bytes; i++) {
		b[(bytes - 1) - i] = value & 0xff;
		value >>= 8;
	}

	if (mcp3564_send_cmd(self, MCP3564_CMD_INCREMENTAL_WRITE | (reg << 2), status, b, bytes, NULL, 0) != MCP3564_RET_OK) {
		return MCP3564_RET_FAILED;
	}

	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_update(Mcp3564 *self) {
	for (uint32_t i = 0; i < MCP3564_REG_MAX; i++) {
		mcp3564_write_reg(self, i, MCP3564_REG_SIZES[i], self->regs[i], NULL);
	}
	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_check(Mcp3564 *self) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_shutdown(Mcp3564 *self) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_select_clock(Mcp3564 *self, enum mcp3564_clock_sel sel) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_sensor_bias(Mcp3564 *self, enum mcp3564_sensor_bias bias) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_mode(Mcp3564 *self, enum mcp3564_mode mode) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_prescaler(Mcp3564 *self, enum mcp3564_prescaler prescaler) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_osr(Mcp3564 *self, enum mcp3564_osr osr) {
	self->regs[MCP3564_REG_CONFIG1] &= ~MCP3564_OSR_MASK;
	self->regs[MCP3564_REG_CONFIG1] |= osr;
	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_set_boost(Mcp3564 *self, enum mcp3564_boost boost) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_gain(Mcp3564 *self, enum mcp3564_gain gain) {
	self->regs[MCP3564_REG_CONFIG2] &= ~MCP3564_GAIN_MASK;
	self->regs[MCP3564_REG_CONFIG2] |= gain;
	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_set_azmux(Mcp3564 *self, bool en) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_conversion_mode(Mcp3564 *self, enum mcp3564_conversion_mode mode) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_data_format(Mcp3564 *self, enum mcp3564_data_format data_format) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_pad_crc_enable(Mcp3564 *self, bool en) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_offcal_enable(Mcp3564 *self, bool en) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_gaincal_enable(Mcp3564 *self, bool en) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_irq_mode(Mcp3564 *self, enum mcp3564_irq_mode mode) {
	self->regs[MCP3564_REG_IRQ] &= ~MCP3564_REG_IRQ_MDAT;
	if (mode == MCP3564_IRQ_MODE_MDAT) {
		self->regs[MCP3564_REG_IRQ] |= MCP3564_REG_IRQ_MDAT;
	}
	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_set_irq_inactive(Mcp3564 *self, enum mcp3564_irq_inactive inactive) {

	return MCP3564_RET_FAILED;
}


mcp3564_ret_t mcp3564_set_fastcmd_enable(Mcp3564 *self, bool en) {
	self->regs[MCP3564_REG_IRQ] &= ~MCP3564_REG_IRQ_EN_FASTCMD;
	if (en) {
		self->regs[MCP3564_REG_IRQ] |= MCP3564_REG_IRQ_EN_FASTCMD;
	}
	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_set_stp_enable(Mcp3564 *self, bool en) {
	self->regs[MCP3564_REG_IRQ] &= ~MCP3564_REG_IRQ_EN_STP;
	if (en) {
		self->regs[MCP3564_REG_IRQ] |= MCP3564_REG_IRQ_EN_STP;
	}
	return MCP3564_RET_OK;
}


mcp3564_ret_t mcp3564_set_mux(Mcp3564 *self, enum mcp3564_mux muxp, enum mcp3564_mux muxm) {
	self->regs[MCP3564_REG_MUX] = (muxp << 4) | muxm;
	return MCP3564_RET_OK;
}




