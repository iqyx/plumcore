/*
 * ON Semiconductor AX5243 Advanced High Performance ASK and FSK Narrow-band
 * Transceiver for 27-1050 MHz Range driver module
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/embedded/umesh)
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

#include "FreeRTOS.h"
#include "task.h"

#include "u_assert.h"
#include "u_log.h"

#include "module.h"
#include "interface_netdev.h"
#include "interface_spidev.h"
#include "ax5243.h"
#include "ax5243_registers.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "ax5243"

/**
 * @todo
 *
 *   - current API doesn't support setting of the pattern matching, preamble and packet length info - hardcode
 *   - fifo handling - reading
 *   - fifo handling - writing
 *   - radio start/stop
 *   - radio state machine
 *   - interrupts
 *   - pn9 testing sequence output
 *
 */


static ax5243_ret_t read_register(Ax5243 *self, uint8_t addr, uint8_t *value) {
	if (u_assert(self != NULL) ||
	    u_assert(value != NULL)) {
		return 0;
	}

	uint8_t txbuf;

	/* The first bit is always 0 to indicate read operation. */
	txbuf = addr & 0x7f;

	if (
		interface_spidev_select(self->spidev) == INTERFACE_SPIDEV_SELECT_OK &&
		interface_spidev_send(self->spidev, &txbuf, 1) == INTERFACE_SPIDEV_SEND_OK &&
		interface_spidev_receive(self->spidev, value, 1) == INTERFACE_SPIDEV_RECEIVE_OK &&
		interface_spidev_deselect(self->spidev) == INTERFACE_SPIDEV_DESELECT_OK
	) {
		return AX5243_RET_OK;
	}

	return AX5243_RET_FAILED;
}


static ax5243_ret_t write_register(Ax5243 *self, uint8_t addr, uint8_t value) {
	if (u_assert(self != NULL)) {
		return AX5243_RET_FAILED;
	}

	uint8_t txbuf[2];

	/* Mark write operation. */
	txbuf[0] = addr | 0x80;
	txbuf[1] = value;

	if (
		interface_spidev_select(self->spidev) == INTERFACE_SPIDEV_SELECT_OK &&
		interface_spidev_send(self->spidev, txbuf, sizeof(txbuf)) == INTERFACE_SPIDEV_SEND_OK &&
		interface_spidev_deselect(self->spidev) == INTERFACE_SPIDEV_DESELECT_OK
	) {
		return AX5243_RET_OK;
	}

	return AX5243_RET_FAILED;
}


static ax5243_ret_t write_fifo(Ax5243 *self, uint8_t *data, size_t len) {
	if (u_assert(self != NULL)) {
		return AX5243_RET_FAILED;
	}

	uint8_t txbuf = AX5243_REG_FIFODATA | 0x80;

	if (
		interface_spidev_select(self->spidev) == INTERFACE_SPIDEV_SELECT_OK &&
		interface_spidev_send(self->spidev, &txbuf, sizeof(txbuf)) == INTERFACE_SPIDEV_SEND_OK &&
		interface_spidev_send(self->spidev, data, len) == INTERFACE_SPIDEV_SEND_OK &&
		interface_spidev_deselect(self->spidev) == INTERFACE_SPIDEV_DESELECT_OK
	) {
		return AX5243_RET_OK;
	}

	return AX5243_RET_FAILED;
}


static ax5243_ret_t write_register_16(Ax5243 *self, uint16_t addr, uint8_t value) {
	if (u_assert(self != NULL)) {
		return AX5243_RET_FAILED;
	}

	uint8_t txbuf[3];

	/* Mark write operation. */
	txbuf[0] = (addr >> 8) | 0xf0;
	txbuf[1] = addr & 0xff;
	txbuf[2] = value;

	if (
		interface_spidev_select(self->spidev) == INTERFACE_SPIDEV_SELECT_OK &&
		interface_spidev_send(self->spidev, txbuf, sizeof(txbuf)) == INTERFACE_SPIDEV_SEND_OK &&
		interface_spidev_deselect(self->spidev) == INTERFACE_SPIDEV_DESELECT_OK
	) {
		return AX5243_RET_OK;
	}

	return AX5243_RET_FAILED;
}


static ax5243_ret_t adjust_frequency(Ax5243 *self, uint32_t freq_hz) {
	if (u_assert(self != NULL)) {
		return AX5243_RET_FAILED;
	}

	if (freq_hz <= 500000000) {
		uint8_t reg;
		if (read_register(self, AX5243_REG_PLLVCODIV, &reg) != AX5243_RET_OK) {
			return AX5243_RET_FAILED;
		}
		reg |= AX5243_REG_PLLVCODIV_RFDIV | AX5243_REG_PLLVCODIV_VCO2INT;
		if (write_register(self, AX5243_REG_PLLVCODIV, reg) != AX5243_RET_OK) {
			return AX5243_RET_FAILED;
		}
	}

	/* If the frequency step is small, do not do VCO autoranging and change
	 * the FREQA registers only. */
	uint32_t freqa = (((uint64_t)freq_hz << 24) / self->fxtal) | 1;
	if (
		write_register(self, AX5243_REG_FREQA3, freqa >> 24) == AX5243_RET_OK &&
		write_register(self, AX5243_REG_FREQA2, (freqa >> 16) & 0xff) == AX5243_RET_OK &&
		write_register(self, AX5243_REG_FREQA1, (freqa >> 8) & 0xff) == AX5243_RET_OK &&
		write_register(self, AX5243_REG_FREQA0, freqa & 0xff) == AX5243_RET_OK
	) {
		return AX5243_RET_OK;
	} else {
		return AX5243_RET_FAILED;
	}
}


static ax5243_ret_t set_frequency(Ax5243 *self, uint32_t freq_hz) {
	if (u_assert(self != NULL)) {
		return AX5243_RET_FAILED;
	}

	/* Frquency step is too big. Set VCO and PLL parameters back to defaults for autoranging
	 * set the radio state to standby, set the frequency registers and start VCO autoranging. */

	write_register(self, AX5243_REG_PLLCPI, 0x08);
	write_register(self, AX5243_REG_PLLLOOP, AX5243_REG_PLLLOOP_FLT_INT_100K | AX5243_REG_PLLLOOP_DIRECT);
	write_register_16(self, AX5243_REG_XTALCAP, 20);
	write_register_16(self, AX5243_REG_PLLRNGCLK, 3);

	write_register(self, AX5243_REG_PWRMODE, AX5243_REG_PWRMODE_XOEN | AX5243_REG_PWRMODE_REFEN | AX5243_REG_PWRMODE_STANDBY);
	write_register(self, AX5243_REG_MODULATION, AX5243_REG_MODULATION_FSK);
	write_register_16(self, AX5243_REG_FSKDEV2, 0);
	write_register_16(self, AX5243_REG_FSKDEV1, 0);
	write_register_16(self, AX5243_REG_FSKDEV0, 0);

	adjust_frequency(self, freq_hz);
	/** @todo wait for oscillator to become ready */
	vTaskDelay(5);

	write_register(self, AX5243_REG_PLLRANGINGA, AX5243_REG_PLLRANGINGA_RNGSTART | 8);
	uint8_t reg;
	do {
		vTaskDelay(1);
		read_register(self, AX5243_REG_PLLRANGINGA, &reg);
	} while (reg & AX5243_REG_PLLRANGINGA_RNGSTART);

	write_register(self, AX5243_REG_PWRMODE, AX5243_REG_PWRMODE_XOEN | AX5243_REG_PWRMODE_REFEN);

	if (reg & AX5243_REG_PLLRANGINGA_RNGERR) {
		return AX5243_RET_FAILED;
	} else {
		return AX5243_RET_OK;
	}
}


const uint8_t pn9[] = {
	0xff, 0xe1, 0x1d, 0x9a, 0xed, 0x85, 0x33, 0x24,
	0xea, 0x7a, 0xd2, 0x39, 0x70, 0x97, 0x57, 0x0a,
	0x54, 0x7d, 0x2d, 0xd8, 0x6d, 0x0d, 0xba, 0x8f,
	0x67, 0x59, 0xc7, 0xa2, 0xbf, 0x34, 0xca, 0x18,
	0x30, 0x53, 0x93, 0xdf, 0x92, 0xec, 0xa7, 0x15,
	0x8a, 0xdc, 0xf4, 0x86, 0x55, 0x4e, 0x18, 0x21,
	0x40, 0xc4, 0xc4, 0xd5, 0xc6, 0x91, 0x8a, 0xcd,
	0xe7, 0xd1, 0x4e, 0x09, 0x32, 0x17, 0xdf, 0x83,
	0xff, 0xf0, 0x0e, 0xcd, 0xf6, 0xc2, 0x19, 0x12,
	0x75, 0x3d, 0xe9, 0x1c, 0xb8, 0xcb, 0x2b, 0x05,
	0xaa, 0xbe, 0x16, 0xec, 0xb6, 0x06, 0xdd, 0xc7,
	0xb3, 0xac, 0x63, 0xd1, 0x5f, 0x1a, 0x65, 0x0c,
	0x98, 0xa9, 0xc9, 0x6f, 0x49, 0xf6, 0xd3, 0x0a,
	0x45, 0x6e, 0x7a, 0xc3, 0x2a, 0x27, 0x8c, 0x10,
	0x20, 0x62, 0xe2, 0x6a, 0xe3, 0x48, 0xc5, 0xe6,
	0xf3, 0x68, 0xa7, 0x04, 0x99, 0x8b, 0xef, 0xc1,
	0x7f, 0x78, 0x87, 0x66, 0x7b, 0xe1, 0x0c, 0x89,
	0xba, 0x9e, 0x74, 0x0e, 0xdc, 0xe5, 0x95, 0x02,
	0x55, 0x5f, 0x0b, 0x76, 0x5b, 0x83, 0xee, 0xe3,
	0x59, 0xd6, 0xb1, 0xe8, 0x2f, 0x8d, 0x32, 0x06,
	0xcc, 0xd4, 0xe4, 0xb7, 0x24, 0xfb, 0x69, 0x85,
	0x22, 0x37, 0xbd, 0x61, 0x95, 0x13, 0x46, 0x08,
	0x10, 0x31, 0x71, 0xb5, 0x71, 0xa4, 0x62, 0xf3,
	0x79, 0xb4, 0x53, 0x82, 0xcc, 0xc5, 0xf7, 0xe0,
	0x3f, 0xbc, 0x43, 0xb3, 0xbd, 0x70, 0x86, 0x44,
	0x5d, 0x4f, 0x3a, 0x07, 0xee, 0xf2, 0x4a, 0x81,
	0xaa, 0xaf, 0x05, 0xbb, 0xad, 0x41, 0xf7, 0xf1,
	0x2c, 0xeb, 0x58, 0xf4, 0x97, 0x46, 0x19, 0x03,
	0x66, 0x6a, 0xf2, 0x5b, 0x92, 0xfd, 0xb4, 0x42,
	0x91, 0x9b, 0xde, 0xb0, 0xca, 0x09, 0x23, 0x04,
	0x88, 0x98, 0xb8, 0xda, 0x38, 0x52, 0xb1, 0xf9,
	0x3c, 0xda, 0x29, 0x41, 0xe6, 0xe2, 0x7b
};

static void test_task(void *p) {
	Ax5243 *self = (Ax5243 *)p;

	vTaskDelay(10);

	self->fxtal = 16000000;


	//~ for (uint32_t f = 400000000; f <= 500000000; f += 100000) {
		//~ if (set_frequency(self, f) == AX5243_RET_OK) {
			//~ u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%u OK"), f);
		//~ } else {
			//~ u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("%u FAILED"), f);
		//~ }
	//~ }
	set_frequency(self, 434034000);

	write_register_16(self, AX5243_REG_TXPWRCOEFFB1, 0x01);
	write_register_16(self, AX5243_REG_TXPWRCOEFFB0, 0x00);

	write_register(self, AX5243_REG_MODULATION, AX5243_REG_MODULATION_MSK);
	write_register_16(self, AX5243_REG_MODCFGF, AX5243_REG_MODCFGF_GAUSSIAN_03);


	uint32_t txrate = 100000;
	uint32_t txratereg = (((uint64_t)txrate << 24) / self->fxtal) | 1;
	write_register_16(self, AX5243_REG_TXRATE2, (txratereg >> 16) & 0xff);
	write_register_16(self, AX5243_REG_TXRATE1, (txratereg >> 8) & 0xff);
	write_register_16(self, AX5243_REG_TXRATE0, txratereg & 0xff);


	uint32_t fdev = 50000;
	uint32_t fdevreg = (((uint64_t)fdev << 24) / self->fxtal) | 1;
	write_register_16(self, AX5243_REG_FSKDEV2, (fdevreg >> 16) & 0xff);
	write_register_16(self, AX5243_REG_FSKDEV1, (fdevreg >> 8) & 0xff);
	write_register_16(self, AX5243_REG_FSKDEV0, fdevreg & 0xff);

	write_register(self, AX5243_REG_ENCODING, 0);
	write_register(self, AX5243_REG_FRAMING, AX5243_REG_FRAMING_FRMMODE_RAWPATTERN | AX5243_REG_FRAMING_CRCMODE_OFF);
	write_register_16(self, AX5243_REG_PKTADDRCFG, AX5243_REG_PKTADDRCFG_MSB_FIRST | AX5243_REG_PKTADDRCFG_FEC_SYNC_DIS);

	write_register(self, AX5243_REG_PWRMODE, AX5243_REG_PWRMODE_XOEN | AX5243_REG_PWRMODE_REFEN | AX5243_REG_PWRMODE_FULLTX);

	//~ uint32_t freq = 433800000;
	while (1) {

		//~ u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("freq = %u"), freq);
		//~ adjust_frequency(self, freq);

		//~ uint8_t irqrequest1, irqrequest0, fifocount;
		//~ read_register(self, AX5243_REG_IRQREQUEST1, &irqrequest1);
		//~ read_register(self, AX5243_REG_IRQREQUEST0, &irqrequest0);
		//~ read_register(self, AX5243_REG_FIFOCOUNT0, &fifocount);
		//~ u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("irqrequest = %02x %02x, fifocount = %u"), irqrequest1, irqrequest0, fifocount);

		//~ write_register(self, AX5243_REG_FIFODATA, AX5243_FIFO_RDATA);

		/* preamble, raw mode */
		//~ uint8_t header[] = {AX5243_FIFO_DATA, 5, 0x28, 0x55, 0x55, 0x55, 0x55};
		//~ write_fifo(self, header, 7);

		/* packet mode */
		//~ uint8_t packet[] = {AX5243_FIFO_DATA, 36, 0x28, 0x72, 0xa9, 32};
		//~ write_fifo(self, packet, 6);
		//~ write_fifo(self, pn9, 32);

		//~ write_register(self, AX5243_REG_FIFOSTAT, AX5243_REG_FIFOSTAT_COMMIT);

		//~ read_register(self, AX5243_REG_FIFOCOUNT0, &fifocount);
		//~ u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("fifocount = %u"), fifocount);

		vTaskDelay(50);
		//~ freq += 500;
		//~ if (freq >= 434700000) {
			//~ break;
		//~ }
	}

	while (1) {

	}
}


static int32_t send(void *context, const uint8_t *buf, size_t len) {
	(void)context;
	(void)buf;
	(void)len;

	Ax5243 *self = (Ax5243 *)context;

	write_register(self, AX5243_REG_PWRMODE, AX5243_REG_PWRMODE_XOEN | AX5243_REG_PWRMODE_REFEN | AX5243_REG_PWRMODE_FULLTX);
	vTaskDelay(2);

	/* preamble, raw mode */
	uint8_t header[] = {AX5243_FIFO_DATA, 5, 0x28, 0x55, 0x55, 0x55, 0x55};
	write_fifo(self, header, 7);

	/* packet mode */
	uint8_t packet[] = {AX5243_FIFO_DATA, len + 4, 0x28, 0x72, 0xa9, len};
	write_fifo(self, packet, 6);
	write_fifo(self, buf, len);

	write_register(self, AX5243_REG_FIFOSTAT, AX5243_REG_FIFOSTAT_COMMIT);
	vTaskDelay(10);
	write_register(self, AX5243_REG_PWRMODE, AX5243_REG_PWRMODE_XOEN | AX5243_REG_PWRMODE_REFEN | AX5243_REG_PWRMODE_POWERDOWN);

	return INTERFACE_NETDEV_SEND_OK;
}


static int32_t receive(void *context, uint8_t *buf, size_t *buf_len, struct interface_netdev_packet_info *info, uint32_t timeout) {
	(void)context;
	(void)buf;
	(void)buf_len;
	(void)info;
	(void)timeout;

	vTaskDelay(100);

	return INTERFACE_NETDEV_RECEIVE_NOT_READY;
}


static int32_t start(void *context) {
	(void)context;

	return INTERFACE_NETDEV_START_OK;
}


static int32_t stop(void *context) {
	(void)context;

	return INTERFACE_NETDEV_STOP_OK;
}


static uint32_t get_capabilities(void *context) {
	(void)context;

	return 0;
}


static int32_t get_param(void *context, enum interface_netdev_param param, int32_t key, int32_t *value) {
	(void)context;
	(void)param;
	(void)key;
	(void)value;

	return INTERFACE_NETDEV_GET_PARAM_OK;
}


static int32_t set_param(void *context, enum interface_netdev_param param, int32_t key, int32_t value) {
	(void)context;
	(void)param;
	(void)key;
	(void)value;

	return INTERFACE_NETDEV_SET_PARAM_OK;
}


static ax5243_ret_t reset(Ax5243 *self) {
	if (u_assert(self != NULL)) {
		return AX5243_RET_FAILED;
	}

	/* Try to reset the chip first. Leave the deep-sleep mode. */
	interface_spidev_select(self->spidev);
	vTaskDelay(5);
	interface_spidev_deselect(self->spidev);

	/* Set and reset the RST bit. */
	if (write_register(self, AX5243_REG_PWRMODE, AX5243_REG_PWRMODE_RST | AX5243_REG_PWRMODE_XOEN | AX5243_REG_PWRMODE_REFEN) != AX5243_RET_OK) {
		return AX5243_RET_FAILED;
	}
	vTaskDelay(1);
	if (write_register(self, AX5243_REG_PWRMODE, AX5243_REG_PWRMODE_XOEN | AX5243_REG_PWRMODE_REFEN) != AX5243_RET_OK) {
		return AX5243_RET_FAILED;
	}

	return AX5243_RET_OK;
}


static ax5243_ret_t check_scratch_rev(Ax5243 *self, uint8_t *rev) {
	if (u_assert(self != NULL) ||
	    u_assert(rev != NULL)) {
		return AX5243_RET_FAILED;
	}

	uint8_t scratch;
	if (
		/* Revision reading must be successful */
		read_register(self, 0, rev) == AX5243_RET_OK &&
		/* and the scratch register must contain data according to the datasheet */
		read_register(self, 1, &scratch) == AX5243_RET_OK &&
		scratch == 0xc5 &&
		/* if yes, write some random value to the scratch register */
		write_register(self, 1, 0x22) == AX5243_RET_OK &&
		/* ...and read it again and check if the value was overwritten correctly */
		read_register(self, 1, &scratch) == AX5243_RET_OK &&
		scratch == 0x22
	) {
		return AX5243_RET_OK;
	}

	return AX5243_RET_FAILED;
}


/* Unknown registers listed either in the datasheet or generated by the AX RadioLab
 * software. Their meaning is mostly unknown, they are just set on the beginning. */
static const struct {
	uint16_t addr;
	uint8_t data;
} unknown_registers[] = {
	{ 0xf00, 0x0f },
	{ 0xf0d, 0x03 },
	{ 0xf10, 0x03 },
	{ 0xf11, 0x07 },
	{ 0xf1c, 0x07 },
	{ 0xf34, 0x28 },
	{ 0xf35, 0x11 },
	{ 0xf44, 0x24 },
	{ 0xf18, 0x06 },
	{ 0, 0 }
};


static ax5243_ret_t set_unknown_registers(Ax5243 *self) {
	if (u_assert(self != NULL)) {
		return AX5243_RET_FAILED;
	}

	for (size_t i = 0; unknown_registers[i].addr != 0; i++) {
		if (write_register(self, unknown_registers[i].addr, unknown_registers[i].data) != AX5243_RET_OK) {
			return AX5243_RET_FAILED;
		}
	}

	return AX5243_RET_OK;
}


ax5243_ret_t ax5243_init(Ax5243 *self, struct interface_spidev *spidev, uint32_t xtal_hz) {
	if (u_assert(self != NULL) ||
	    u_assert(spidev != NULL)) {
		return AX5243_RET_FAILED;
	}

	/* Initialize network device interface. */
	interface_netdev_init(&(self->iface));
	self->iface.vmt.context = (void *)self;
	self->iface.vmt.send = send;
	self->iface.vmt.receive = receive;
	self->iface.vmt.start = start;
	self->iface.vmt.stop = stop;
	self->iface.vmt.get_capabilities = get_capabilities;
	self->iface.vmt.get_param = get_param;
	self->iface.vmt.set_param = set_param;

	self->spidev = spidev;
	self->fxtal = xtal_hz;

	if (reset(self) != AX5243_RET_OK) {
		goto err;
	}

	uint8_t rev;
	if (check_scratch_rev(self, &rev) != AX5243_RET_OK) {
		goto err;
	}

	if (set_unknown_registers(self) != AX5243_RET_OK) {
		goto err;
	}

	xTaskCreate(test_task, "test-task", configMINIMAL_STACK_SIZE + 256, (void *)self, 2, NULL);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized, chip revision 0x%02x"), rev);

	return AX5243_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("initialization failed"));
	return AX5243_RET_FAILED;
}


ax5243_ret_t ax5243_free(Ax5243 *self) {
	if (u_assert(self != NULL)) {
		return AX5243_RET_FAILED;
	}

	/* Do nothing. */

	return AX5243_RET_OK;
}
