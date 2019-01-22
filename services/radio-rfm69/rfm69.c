/*
 * HopeRF RFM69CW/W/HW module driver
 *
 * Copyright (c) 2014-2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "u_assert.h"
#include "u_log.h"

#include "FreeRTOS.h"
#include "interfaces/radio.h"
#include "interface_spidev.h"
#include "rfm69.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "rfm69"


#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))


static rfm69_ret_t register_write(Rfm69 *self, enum rfm69_register addr, uint8_t value) {
	if (u_assert(self != NULL)) {
		return RFM69_RET_NULL;
	}

	uint8_t txbuf[2];

	/* The first bit is always 1 to indicate write operation. */
	txbuf[0] = addr | 0x80;
	txbuf[1] = value;

	interface_spidev_select(self->spidev);
	interface_spidev_send(self->spidev, txbuf, sizeof(txbuf));
	interface_spidev_deselect(self->spidev);

	return RFM69_RET_OK;
}


static uint8_t register_read(Rfm69 *self, enum rfm69_register addr) {
	if (u_assert(self != NULL)) {
		return 0;
	}

	uint8_t txbuf, rxbuf;

	/* The first bit is always 0 to indicate read operation. */
	txbuf = addr & 0x7f;

	interface_spidev_select(self->spidev);
	interface_spidev_send(self->spidev, &txbuf, 1);
	interface_spidev_receive(self->spidev, &rxbuf, 1);
	interface_spidev_deselect(self->spidev);

	return rxbuf;
}


static uint16_t register_read16(Rfm69 *self, enum rfm69_register addr) {
	if (u_assert(self != NULL)) {
		return 0;
	}

	uint8_t txbuf, rxbuf[2];

	/* The first bit is always 0 to indicate read operation. */
	txbuf = addr & 0x7f;

	interface_spidev_select(self->spidev);
	interface_spidev_send(self->spidev, &txbuf, 1);
	interface_spidev_receive(self->spidev, rxbuf, 2);
	interface_spidev_deselect(self->spidev);

	return rxbuf[0] << 8 | rxbuf[1];
}


static rfm69_ret_t fifo_write(Rfm69 *self, const uint8_t *buf, uint8_t len) {
	if (u_assert(self != NULL)) {
		return RFM69_RET_NULL;
	}
	if (u_assert(buf != NULL) ||
	    u_assert(len > 0)) {
		return RFM69_RET_BAD_ARG;
	}
	/** @todo writing more than 32B to the fifo apparently fails. */
	if (len > 32) {
		return RFM69_RET_FAILED;
	}

	/* Write operation to the address 0 (FIFO). */
	uint8_t txbuf = 0x80;
	interface_spidev_select(self->spidev);
	interface_spidev_send(self->spidev, &txbuf, 1);
	interface_spidev_send(self->spidev, buf, len);
	interface_spidev_deselect(self->spidev);

	/** @todo check for overflow */

	return RFM69_RET_OK;
}


static rfm69_ret_t fifo_read(Rfm69 *self, uint8_t *buf, uint8_t len) {
	if (u_assert(self != NULL)) {
		return RFM69_RET_NULL;
	}
	if (u_assert(buf != NULL) ||
	    u_assert(len > 0)) {
		return RFM69_RET_BAD_ARG;
	}

	/** @todo check if there are at least @p len bytes available in the FIFO */

	/* Read operation from the address 0 (FIFO). */
	uint8_t txbuf = 0x00;
	interface_spidev_select(self->spidev);
	interface_spidev_send(self->spidev, &txbuf, 1);
	interface_spidev_receive(self->spidev, buf, len);
	interface_spidev_deselect(self->spidev);

	return RFM69_RET_OK;
}


/* A bitmask of enum rfm69_rstatus is returned. */
static uint32_t get_status(Rfm69 *self) {
	return register_read16(self, RFM69_REG_IRQFLAGS1);
}


static enum rfm69_rmode mode_get(Rfm69 *self) {
	return (register_read(self, RFM69_REG_OPMODE) >> 2) & 3;
}


static void mode_set_listen(Rfm69 *self, enum rfm69_rmode mode, bool listen, bool abort) {
	uint8_t reg = 0;
	reg |= (mode & 7) << 2;

	if (listen) {
		reg |= 0x40;
	}
	if (abort) {
		reg |= 0x20;
	}
	register_write(self, RFM69_REG_OPMODE, reg);
}


static void mode_set(Rfm69 *self, enum rfm69_rmode mode) {
	/* Sequencer always enabled, listen mode always disabled, listenabort = 0, reserved = 0 */
	// register_write(self, RFM69_REG_OPMODE, (mode & 7) << 2);
	mode_set_listen(self, mode, false, true);
	mode_set_listen(self, mode, false, false);
}


/* Part of the IRadio interface. */
static iradio_ret_t iradio_send(void *context, const uint8_t *buf, size_t len, const struct iradio_send_params *params) {
	if (u_assert(context != NULL)) {
		return IRADIO_RET_NULL;
	}

	(void)params;
	Rfm69 *self = (Rfm69 *)context;

	uint32_t data_pos = 0;

	/* Check if the driver state is IDLE, return if not. */
	if (!(self->driver_state == RFM69_STATE_IDLE)) {
		return IRADIO_RET_BAD_STATE;
	}

	/* FIFO should be prefilled here with packet length (1B) and up to 30B of
	 * packet data (<32B to avoid setting FIFO threshold flag). Then the driver waits
	 * for either a TXREADY interrupt or a next poll event, which is at most 31B time.
	 * If nothing happens, the FIFO will underrun eventually. */
	uint8_t plen = (uint8_t)(len);
	fifo_write(self, &plen, 1);

	uint8_t wbytes = MIN(30, len - data_pos);
	if (fifo_write(self, buf + data_pos, wbytes) == RFM69_RET_OK) {
		/* Move forward only if the write operation succeeded. */
		data_pos += wbytes;
	}

	/* Mode is changed to TXREADY AFTER the FIFO is prefilled.
	 * Change driver state to allow interrupts to be passed here. */
	self->driver_state = RFM69_STATE_TRANSMITTING;

	/* Wait for a poll event or an interrupt. */
	/* If the semaphore is signaled from the interrupt, continue. If the interrupt is
	 * not fired, broken or not used at all, wait 10 ticks and continue. This is long
	 * enough and makes things not optimal, But at least it works somehow. */
	xSemaphoreTake(self->semaphore, (TickType_t)RFM69_DEFAULT_TIMEOUT_TICS);

	mode_set(self, RFM69_RMODE_TX);

	if (self->tx_led != NULL) {
		interface_led_once(self->tx_led, 0x11);
	}

	/** @todo add upper margin (timeout) */
	while (true) {
		xSemaphoreTake(self->semaphore, (TickType_t)RFM69_DEFAULT_TIMEOUT_TICS);

		uint32_t driver_status = get_status(self);

		/* Check if the packet has been sent. Break and return if yes. */
		if (driver_status & RFM69_RSTATUS_PACKETSENT) {
			break;
		}

		/* If the packet is not sent yet, Check the TXREADY flag,
		 * return error if not set (the transmission didn't even start). */
		if (!(driver_status & RFM69_RSTATUS_TXREADY)) {
			mode_set(self, RFM69_RMODE_SLEEP);
			self->driver_state = RFM69_STATE_IDLE;

			return IRADIO_RET_BAD_STATE;
		}

		/* FIFO is empty - underrun detected, packet was sent corrupted. */
		if (!(driver_status & RFM69_RSTATUS_FIFONOTEMPTY)) {
			mode_set(self, RFM69_RMODE_SLEEP);
			self->driver_state = RFM69_STATE_IDLE;

			return IRADIO_RET_FAILED;
		}

		/* Check the FIFO threshold flag, send another 32 bytes if not set
		 * (that means there is space for at least 34 more bytes as the FIFO is 66 byte long). */
		if (!(driver_status & RFM69_RSTATUS_FIFOLEVEL)) {
			if ((len - data_pos) > 0) {
				wbytes = MIN(32, len - data_pos);
				if (fifo_write(self, buf + data_pos, wbytes) == RFM69_RET_OK) {
					data_pos += wbytes;
				}
			}
		}
	}

	/* The packet has been successfully sent, change driver state back to idle. */
	mode_set(self, RFM69_RMODE_SLEEP);
	self->driver_state = RFM69_STATE_IDLE;

	return IRADIO_RET_OK;
}


/** @todo common exit handlers (OK and fail) */
/* Part of the IRadio interface. */
static iradio_ret_t iradio_receive(void *context, uint8_t *buf, size_t buf_len, size_t *received_len, struct iradio_receive_params *params, uint32_t timeout_us) {
	if (u_assert(context != NULL)) {
		return IRADIO_RET_NULL;
	}
	if (u_assert(buf != NULL) ||
	    u_assert(buf_len > 0) ||
	    u_assert(received_len != NULL)) {
		return IRADIO_RET_BAD_ARG;
	}
	Rfm69 *self = (Rfm69 *)context;

	/* Initialize length to -1 to indicate that no length was pulled from the FIFO yet. */
	int32_t len = -1;
	uint32_t data_pos = 0;

	/* Check if the driver state is idle, return otherwise. */
	if (!(self->driver_state == RFM69_STATE_IDLE)) {
		return IRADIO_RET_BAD_STATE;
	}

	/* Configure the radio receiving parameters and modulation.
	 * The register contains timeout value in units of 16 * bit time. */
	// uint32_t rxtimeout_bits = timeout_us * self->bit_rate_bps / 1000000UL;
	// register_write(self, RFM69_REG_RXTIMEOUT2, rxtimeout_bits / 16);
	register_write(self, RFM69_REG_RXTIMEOUT2, 0x00);

	/* Set the radio mode to RX first. */
	mode_set(self, RFM69_RMODE_RX);
	self->driver_state = RFM69_STATE_RECEIVING;

	/* Auto restart. */
	register_write(self, RFM69_REG_PACKETCONFIG2, 0xf6);

	// register_write(self, RFM69_REG_LISTEN1, 0x50);
	// register_write(self, RFM69_REG_LISTEN2, 80); /* 64us * 30 =~ 2ms idle */
	// register_write(self, RFM69_REG_LISTEN3, 10); /* 64us * 10 =~ 640us RX */
	// mode_set_listen(self, RFM69_RMODE_SLEEP, true, false);
	// self->driver_state = RFM69_STATE_RECEIVING;

	int32_t t_us = 10000;
	while (true) {
		uint32_t driver_status = get_status(self);

		/** @todo configure the hardware timeout to the @p timeout value */
		// if ((driver_status & RFM69_RSTATUS_TIMEOUT) || (t_us < 0)) {
		if (t_us < 0) {
			mode_set(self, RFM69_RMODE_SLEEP);
			self->driver_state = RFM69_STATE_IDLE;

			return IRADIO_RET_TIMEOUT;
		}
		if (driver_status & RFM69_RSTATUS_RXREADY) {
			break;
		}

		/* Wait on the semaphore otherwise. */
		xSemaphoreTake(self->semaphore, (TickType_t)RFM69_DEFAULT_TIMEOUT_TICS);
		/** @todo convert tics to us */
		t_us -= RFM69_DEFAULT_TIMEOUT_TICS * 1000;
	}

	/* Trigger FEI measurement. */
	register_write(self, RFM69_REG_AFCFEI, 0x20);

	/* Timeout to start of the reception. */
	t_us = timeout_us;
	while (true) {
		uint32_t driver_status = get_status(self);

		// if ((driver_status & RFM69_RSTATUS_TIMEOUT) || (t_us < 0)) {
		if (t_us < 0) {
			mode_set(self, RFM69_RMODE_SLEEP);
			self->driver_state = RFM69_STATE_IDLE;

			return IRADIO_RET_TIMEOUT;
		}

		if (driver_status & RFM69_RSTATUS_FIFONOTEMPTY) {
			/* We don't know the length yet, read it. */
			if (len == -1) {
				uint8_t plen;
				fifo_read(self, &plen, 1);
				len = plen;

				if (self->rx_led != NULL) {
					interface_led_once(self->rx_led, 0x11);
				}

				/* Packet reception started, restart timeout to the maximum packet length. */
				t_us = 256 * 8 * 1000000 / self->bit_rate_bps;

				/* We need to reread the status again (to refresh the FIFOLEVEL status). */
				continue;
			}
		}

		/* If there is anything to read. */
		if ((len - data_pos) > 0) {
			/* If the FIFO threshold flag is set, we can safely read at least 32 bytes. */
			if (driver_status & RFM69_RSTATUS_FIFOLEVEL) {
				if ((len - data_pos) < 32) {
					/* Something went wrong, we have 32B but we should not read that much. */
					mode_set(self, RFM69_RMODE_SLEEP);
					self->driver_state = RFM69_STATE_IDLE;

					return IRADIO_RET_FAILED;
				}

				uint8_t rbytes = 32;
				if (fifo_read(self, buf + data_pos, rbytes) == RFM69_RET_OK) {
					data_pos += rbytes;
				}
				continue;
			}

			/* If the PAYLOAD_READY flag is set, all packet data are in the fifo, read till the end. */
			if (driver_status & RFM69_RSTATUS_PAYLOADREADY) {
				uint8_t rbytes = len - data_pos;
				if (fifo_read(self, buf + data_pos, rbytes) == RFM69_RET_OK) {
					data_pos += rbytes;
				}

				/* There should be no bytes left. */
				if ((len - data_pos) > 0) {
					mode_set(self, RFM69_RMODE_SLEEP);
					self->driver_state = RFM69_STATE_IDLE;

					return IRADIO_RET_FAILED;
				}
				break;
			}

			/* If neither FIFO threshold is set nor payload is ready, check if we have
			 * at least 1 byte available in the FIFO and read it. */
			if (driver_status & RFM69_RSTATUS_FIFONOTEMPTY) {

				if (fifo_read(self, buf + data_pos, 1) == RFM69_RET_OK) {
					data_pos++;
				}
				continue;
			}
		} else {
			/* There are no more bytes to read. */
			break;
		}

		xSemaphoreTake(self->semaphore, (TickType_t)RFM69_DEFAULT_TIMEOUT_TICS);
		/** @todo convert tics to us */
		t_us -= RFM69_DEFAULT_TIMEOUT_TICS * 1000;
	}

	/* Sample RSSI and FEI values. */
	if (params != NULL) {
		params->rssi_dbm = -((int)(register_read(self, RFM69_REG_RSSIVALUE))) * 5;

		/* The FEI value is 16bit 2's complement. */
		int16_t fei = (int16_t)(register_read(self, RFM69_REG_FEIMSB) << 8 |
		              register_read(self, RFM69_REG_FEILSB));
		params->freq_error_hz = (int32_t)(fei * 61);
	}

	mode_set(self, RFM69_RMODE_SLEEP);
	self->driver_state = RFM69_STATE_IDLE;

	*received_len = len;

	return IRADIO_RET_OK;
}


rfm69_ret_t rfm69_start(Rfm69 *self) {
	if (u_assert(self != NULL)) {
		return RFM69_RET_NULL;
	}

	if (self->driver_state != RFM69_STATE_STOPPED) {
		return RFM69_RET_FAILED;
	}

	self->driver_state = RFM69_STATE_IDLE;
	return RFM69_RET_OK;
}


rfm69_ret_t rfm69_stop(Rfm69 *self) {
	if (u_assert(self != NULL)) {
		return RFM69_RET_NULL;
	}
	if (self->driver_state != RFM69_STATE_IDLE) {
		return RFM69_RET_FAILED;
	}

	self->driver_state = RFM69_STATE_STOPPED;
	return RFM69_RET_OK;
}


/* Part of the IRadio interface. */
static iradio_ret_t iradio_get_frequency(void *context, uint64_t *freq_hz) {
	Rfm69 *self = (Rfm69 *)context;
	if (u_assert(self != NULL)) {
		return IRADIO_RET_NULL;
	}
	if (u_assert(freq_hz != NULL)) {
		return IRADIO_RET_BAD_ARG;
	}

	uint32_t reg = 0;

	reg |= register_read(self, RFM69_REG_FRFMSB) << 16;
	reg |= register_read(self, RFM69_REG_FRFMID) << 8;
	reg |= register_read(self, RFM69_REG_FRFLSB);

	*freq_hz = (uint64_t)reg * 61ULL;

	return IRADIO_RET_OK;
}


/* Part of the IRadio interface. */
static iradio_ret_t iradio_set_frequency(void *context, uint64_t freq_hz) {
	Rfm69 *self = (Rfm69 *)context;
	if (u_assert(self != NULL)) {
		return IRADIO_RET_NULL;
	}
	/* TODO: proper frequency checking */
	if (freq_hz == 0) {
		return IRADIO_RET_BAD_ARG;
	}

	/* FRF register value = freq in Hz / 61Hz (one step) */
	uint32_t reg = freq_hz / 61ULL;
	register_write(self, RFM69_REG_FRFMSB, (reg >> 16) & 0xff);
	register_write(self, RFM69_REG_FRFMID, (reg >> 8) & 0xff);
	register_write(self, RFM69_REG_FRFLSB, reg & 0xff);

	/* Read the register back to check. */
	uint32_t rreg = 0;
	rreg |= register_read(self, RFM69_REG_FRFMSB) << 16;
	rreg |= register_read(self, RFM69_REG_FRFMID) << 8;
	rreg |= register_read(self, RFM69_REG_FRFLSB);

	if (reg != rreg) {
		return IRADIO_RET_FAILED;
	}

	return IRADIO_RET_OK;
}


/* Part of the IRadio interface. */
static iradio_ret_t iradio_get_tx_power(void *context, int32_t *power_dbm) {
	Rfm69 *self = (Rfm69 *)context;
	if (u_assert(self != NULL)) {
		return IRADIO_RET_NULL;
	}
	if (u_assert(power_dbm != NULL)) {
		return IRADIO_RET_BAD_ARG;
	}

	uint32_t reg = register_read(self, RFM69_REG_PALEVEL);
	*power_dbm = (reg & 0x1f) - 18;

	return IRADIO_RET_OK;
}


/* Part of the IRadio interface. */
static iradio_ret_t iradio_set_tx_power(void *context, int32_t power_dbm) {
	Rfm69 *self = (Rfm69 *)context;
	if (u_assert(self != NULL)) {
		return IRADIO_RET_NULL;
	}

	if (power_dbm < -18 || power_dbm > 13) {
		return IRADIO_RET_BAD_ARG;
	}

	register_write(self, RFM69_REG_PALEVEL, (uint8_t)(0x80 | ((power_dbm + 18) & 0x1f)));

	/* Read the txpower value back to check it. */
	int32_t power_r;
	if (iradio_get_tx_power((void *)self, &power_r) != IRADIO_RET_OK) {
		return IRADIO_RET_FAILED;
	}

	if (power_dbm != power_r) {
		return IRADIO_RET_FAILED;
	}

	return IRADIO_RET_OK;
}


/* Part of the IRadio interface. */
static iradio_ret_t iradio_set_bit_rate(void *context, uint32_t bit_rate_bps) {
	Rfm69 *self = (Rfm69 *)context;
	if (u_assert(self != NULL)) {
		return IRADIO_RET_NULL;
	}

	uint16_t reg = RFM69_XTAL_FREQ / bit_rate_bps;

	register_write(self, RFM69_REG_BITRATEMSB, reg >> 8);
	register_write(self, RFM69_REG_BITRATELSB, reg & 0xff);

	self->bit_rate_bps = bit_rate_bps;

	return IRADIO_RET_OK;
}


static iradio_ret_t iradio_set_preamble(void *context, uint32_t length_bits) {
	Rfm69 *self = (Rfm69 *)context;
	if (u_assert(self != NULL)) {
		return IRADIO_RET_NULL;
	}

	uint16_t bytes = length_bits / 8 + 1;

	register_write(self, RFM69_REG_PREAMBLEMSB, bytes >> 8);
	register_write(self, RFM69_REG_PREAMBLELSB, bytes & 0xff);

	return IRADIO_RET_OK;
}


static iradio_ret_t iradio_set_sync(void *context, const uint8_t *bytes, size_t sync_length) {
	Rfm69 *self = (Rfm69 *)context;
	if (u_assert(self != NULL)) {
		return IRADIO_RET_NULL;
	}

	if (sync_length < 1 || sync_length > 8) {
		return IRADIO_RET_NOT_IMPLEMENTED;
	}

	uint8_t sync_config = 0;

	/* Set sync tolerance to 0. */
	sync_config |= 0;

	/* Set sync size. */
	sync_config |= ((sync_length - 1) << 3);

	/* Enable sync generation and detection. */
	sync_config |= 0x80;

	register_write(self, RFM69_REG_SYNCCONFIG, sync_config);

	for (size_t i = 0; i < sync_length; i++) {
		register_write(self, RFM69_REG_SYNCVALUE + i, bytes[i]);
	}

	return IRADIO_RET_OK;
}


static rfm69_ret_t rfm69_check_version(Rfm69 *self) {
	if (u_assert(self != NULL)) {
		return RFM69_RET_NULL;
	}

	uint8_t v = register_read(self, RFM69_REG_VERSION);
	if (v == 0) {
		return RFM69_RET_FAILED;
	}

	uint8_t rev = v >> 4;
	uint8_t mask = v & 0x0f;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("SX1231 detected, revision = %d, mask = %d"), rev, mask);

	return RFM69_RET_OK;
}


rfm69_ret_t rfm69_init(Rfm69 *self, struct interface_spidev *spidev) {
	if (u_assert(self != NULL)) {
		return RFM69_RET_NULL;
	}
	if (u_assert(spidev != NULL)) {
		return RFM69_RET_BAD_ARG;
	}

	memset(self, 0, sizeof(Rfm69));

	if (iradio_init(&self->radio) != IRADIO_RET_OK) {
		goto err;
	}
	self->radio.vmt.context = (void *)self;
	self->radio.vmt.send = iradio_send;
	self->radio.vmt.receive = iradio_receive;
	self->radio.vmt.set_tx_power = iradio_set_tx_power;
	self->radio.vmt.set_frequency = iradio_set_frequency;
	self->radio.vmt.set_bit_rate = iradio_set_bit_rate;
	self->radio.vmt.set_sync = iradio_set_sync;

	/* Init references to other modules. */
	self->spidev = spidev;
	self->tx_led = NULL;
	self->rx_led = NULL;

	self->semaphore = xSemaphoreCreateBinary();
	if (self->semaphore == NULL) {
		goto err;
	}

	if (rfm69_check_version(self) != RFM69_RET_OK) {
		goto err;
	}

	/* Set some default parameters which should not harm anything. */
	iradio_set_bit_rate((void *)self, 50000);
	iradio_set_frequency((void *)self, 433920000);
	iradio_set_tx_power((void *)self, -10);
	iradio_set_preamble((void *)self, 8);
	const uint8_t sync[2] = {0x72, 0xa9};
	iradio_set_sync((void *)self, sync, 2);

	/** @todo move to iradio_set_modulation function */
	/* set packet mode, FSK modulation, BT=0.5 = 00000010 (0x02) */
	register_write(self, RFM69_REG_DATAMODUL, 0x03);
	/* set fdev to 12500Hz */
	register_write(self, RFM69_REG_FDEVMSB, 0x01); //0x00
	register_write(self, RFM69_REG_FDEVLSB, 0x9a); //0xcd
	/* set receiver bandwidth to 100kHz single sideband */
	register_write(self, RFM69_REG_RXBW, 0xeb); //2c
	register_write(self, RFM69_REG_AFCBW, 0xeb);

	/* Set RSSI timeout to 0 (disable). We do not use it.
	 * PayloadReady timeout defaults to maximum. */
	register_write(self, RFM69_REG_RXTIMEOUT1, 0x00);
	register_write(self, RFM69_REG_RXTIMEOUT2, 0xff);

	/** @todo keep here */
	/* set lna input impedance and AGC */
	register_write(self, 0x18, 0x80); //80

	/* DIO mapping */
	register_write(self, 0x25, 0x40);
	register_write(self, 0x26, 0x30);

	/* set rssi threshold to -110dBm */
	register_write(self, 0x29, 220);

	/* variable length packet, no dc-free encoding, no crc, no address filtering */
	register_write(self, RFM69_REG_PACKETCONFIG1, 0xc0); /* 0x80 */

	/* Maximum packet size. */
	register_write(self, RFM69_REG_PAYLOADLENGTH, 0xff);

	/* set fifo threshold to 32 bytes */
	register_write(self, 0x3c, 0xa0);

	/* interpacket delay off, auto rx restart, aes off */
	register_write(self, 0x3d, 0xf2);

	/* turn on sensitivity boost */
	register_write(self, 0x58, 0x2d); //2d

	/* turn dagc on */
	register_write(self, 0x6f, 0x30); //30

	/* RC oscillator calibration. */
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("calibrating internal RC oscillator"));
	register_write(self, RFM69_REG_OSC1, 0x80);
	while (!(register_read(self, RFM69_REG_OSC1) & 0x40)) {
		vTaskDelay(2);
	}

	/* Set default driver state. */
	self->driver_state = RFM69_STATE_IDLE;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("RFM69CW/W/HW module driver initialized (defaults)"));

	return RFM69_RET_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR,
		U_LOG_MODULE_PREFIX("error while initializing the RFM69CW/W/HW module driver")
	);

	return RFM69_RET_FAILED;
}


rfm69_ret_t rfm69_free(Rfm69 *self) {
	if (u_assert(self != NULL)) {

	}
	if (self->driver_state != RFM69_STATE_IDLE) {
		return RFM69_RET_FAILED;
	}

	/** @todo */
	self->driver_state = RFM69_STATE_UNINITIALIZED;

	return RFM69_RET_OK;
}


rfm69_ret_t rfm69_interrupt_handler(Rfm69 *self) {
	if (u_assert(self != NULL)) {
		return RFM69_RET_NULL;
	}
	if (self->driver_state == RFM69_STATE_UNINITIALIZED) {
		return RFM69_RET_FAILED;
	}

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	switch (self->driver_state) {
		case RFM69_STATE_TRANSMITTING:
		case RFM69_STATE_RECEIVING:
			xSemaphoreGiveFromISR(self->semaphore, &xHigherPriorityTaskWoken);
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			break;

		case RFM69_STATE_IDLE:
		default:
			break;
	};

	return RFM69_RET_OK;
}


rfm69_ret_t rfm69_set_led(Rfm69 *self, enum rfm69_led_type type, struct interface_led *led) {
	if (u_assert(self != NULL)) {
		return RFM69_RET_NULL;
	}
	if (u_assert(led != NULL)) {
		return RFM69_RET_BAD_ARG;
	}

	switch (type) {
		case RFM69_LED_TYPE_ALL:
			self->rx_led = led;
			self->tx_led = led;
			break;

		case RFM69_LED_TYPE_TX:
			self->tx_led = led;
			break;

		case RFM69_LED_TYPE_RX:
			self->rx_led = led;
			break;

		default:
			return RFM69_RET_FAILED;
	}

	return RFM69_RET_OK;
}


