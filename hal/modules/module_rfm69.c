/**
 * HopeRF RFM69CW/W/HW module driver
 *
 * Copyright (C) 2014, Marek Koza, qyx@krtko.org
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

#include "FreeRTOS.h"
#include "task.h"
#include "u_assert.h"
#include "u_log.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "hal_module.h"
#include "interface_netdev.h"
#include "interface_spidev.h"
#include "module_rfm69.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))


static void module_rfm69_register_write(struct module_rfm69 *rfm69, uint8_t addr, uint8_t value) {
	if (u_assert(rfm69 != NULL)) {
		return;
	}

	uint8_t txbuf[2];

	/* first bit is always 1 to indicate write operation */
	txbuf[0] = addr | 0x80;
	txbuf[1] = value;

	interface_spidev_select(rfm69->spidev);
	interface_spidev_send(rfm69->spidev, txbuf, sizeof(txbuf));
	interface_spidev_deselect(rfm69->spidev);
}


static uint8_t module_rfm69_register_read(struct module_rfm69 *rfm69, uint8_t addr) {
	if (u_assert(rfm69 != NULL)) {
		return 0;
	}

	uint8_t txbuf, rxbuf;

	/* first bit is always 0 to indicate read operation */
	txbuf = addr & 0x7f;

	interface_spidev_select(rfm69->spidev);
	interface_spidev_send(rfm69->spidev, &txbuf, 1);
	interface_spidev_receive(rfm69->spidev, &rxbuf, 1);
	interface_spidev_deselect(rfm69->spidev);

	return rxbuf;
}


static int32_t module_rfm69_fifo_write(struct module_rfm69 *rfm69, const uint8_t *buf, uint8_t len) {
	if (u_assert(rfm69 != NULL)) {
		return -1;
	}

	/* Huh? */
	if (len > 32) {
		return 0;
	}

	/* Write operation to address 0 (FIFO). */
	uint8_t txbuf = 0x80;
	interface_spidev_select(rfm69->spidev);
	interface_spidev_send(rfm69->spidev, &txbuf, 1);
	interface_spidev_send(rfm69->spidev, buf, len);
	interface_spidev_deselect(rfm69->spidev);

	return len;
}


static int32_t module_rfm69_fifo_read(struct module_rfm69 *rfm69, uint8_t *buf, uint8_t len) {

	uint8_t txbuf = 0x00;
	interface_spidev_select(rfm69->spidev);
	interface_spidev_send(rfm69->spidev, &txbuf, 1);
	interface_spidev_receive(rfm69->spidev, buf, len);
	interface_spidev_deselect(rfm69->spidev);

	return len;
}


static int32_t module_rfm69_get_status(struct module_rfm69 *rfm69) {
	return (module_rfm69_register_read(rfm69, 0x27) << 8) | module_rfm69_register_read(rfm69, 0x28);
}


static int32_t module_rfm69_mode_get(struct module_rfm69 *rfm69) {
	return (module_rfm69_register_read(rfm69, 0x01) >> 2) & 3;
}


static void module_rfm69_mode_set(struct module_rfm69 *rfm69, int32_t mode) {
	/* sequencer always enabled, listen mode always disabled,
	 * listenabort = 0, reserved = 0 */
	module_rfm69_register_write(rfm69, 0x01, (mode & 7) << 2);
}


static int32_t module_rfm69_send(void *context, const uint8_t *buf, size_t len) {
	if (u_assert(context != NULL)) {
		return INTERFACE_NETDEV_SEND_FAILED;
	}
	struct module_rfm69 *rfm69 = (struct module_rfm69 *)context;

	uint32_t data_pos = 0;

	/* check if driver state is idle, return otherwise */
	if (!(rfm69->driver_state == RFM69_STATE_IDLE)) {
		return INTERFACE_NETDEV_SEND_NOT_READY;
	}

	/* Mode is changed to TXREADY first to begin transmission as soon as possible
	 * (to make listen-before-talk effective. Also change driver state to allow
	 * interrupts to be passed here */
	rfm69->driver_state = RFM69_STATE_TRANSMITTING;

	/* wait for poll event or interrupt */
	/* TODO: check the return value */
	xSemaphoreTake(rfm69->semaphore, (TickType_t)10);

	module_rfm69_mode_set(rfm69, RFM69_RMODE_TX);

	if (rfm69->tx_led != NULL) {
		interface_led_once(rfm69->tx_led, 0x11);
	}

	/* FIFO should be prefilled here with packet length (1B) and up to 30B of
	 * packet data (<32B to avoid setting FIFO threshold flag). Then the driver waits
	 * for either TXREADY interrupt or next poll event, which is at most 31B time. */
	uint8_t plen = (uint8_t)(len);
	module_rfm69_fifo_write(rfm69, &plen, 1);
	data_pos += module_rfm69_fifo_write(rfm69, buf + data_pos, MIN(30, len - data_pos));

	/* TODO: add upper margin (timeout) */
	while (1) {
		/* wait for poll event or interrupt */
		/* TODO: check the return value */
		xSemaphoreTake(rfm69->semaphore, (TickType_t)10);

		int32_t driver_status = module_rfm69_get_status(rfm69);

		/* check if packet has already been sent */
		if (driver_status & RFM69_RSTATUS_PACKETSENT) {
			break;
		}

		/* If the packet is not sent yet, Check for TXREADY flag,
		 * return error if not set (transmission didn't even start) */
		if (!(driver_status & RFM69_RSTATUS_TXREADY)) {
			module_rfm69_mode_set(rfm69, RFM69_RMODE_SLEEP);
			rfm69->driver_state = RFM69_STATE_IDLE;

			return INTERFACE_NETDEV_SEND_NOT_READY;
		}

		/* check FIFO threshold flag, send another 32 bytes if not set (there is
		 * space for at least 34 more bytes as FIFO is 66 byte long) */
		if (!(driver_status & RFM69_RSTATUS_FIFOLEVEL)) {
			if ((len - data_pos) > 0) {
				data_pos += module_rfm69_fifo_write(rfm69, buf + data_pos, MIN(32, len - data_pos));
			}
		}

		/* FIFO underrun detected, packet was sent corrupted */
		if (!(driver_status & RFM69_RSTATUS_FIFONOTEMPTY)) {
			module_rfm69_mode_set(rfm69, RFM69_RMODE_SLEEP);
			rfm69->driver_state = RFM69_STATE_IDLE;

			return INTERFACE_NETDEV_SEND_FAILED;
		}
	}

	/* packet has been successfully sent, change driver state back to idle */
	module_rfm69_mode_set(rfm69, RFM69_RMODE_SLEEP);
	rfm69->driver_state = RFM69_STATE_IDLE;

	return INTERFACE_NETDEV_SEND_OK;
}


static int32_t module_rfm69_receive(void *context, uint8_t *buf, size_t *buf_len, struct interface_netdev_packet_info *info, uint32_t timeout) {
	if (u_assert(context != NULL && buf != NULL && buf_len != NULL)) {
		return INTERFACE_NETDEV_RECEIVE_FAILED;
	}
	struct module_rfm69 *rfm69 = (struct module_rfm69 *)context;
	(void)timeout;

	/* initialize length to -1 to indicate that no length was pulled from FIFO yet */
	int32_t len = -1;
	uint32_t data_pos = 0;

	/* check if driver state is idle, return otherwise */
	if (!(rfm69->driver_state == RFM69_STATE_IDLE)) {
		return INTERFACE_NETDEV_RECEIVE_NOT_READY;
	}

	/* set radio mode to RX first */
	module_rfm69_mode_set(rfm69, RFM69_RMODE_RX);
	rfm69->driver_state = RFM69_STATE_RECEIVING;

	/* auto restart */
	module_rfm69_register_write(rfm69, 0x3d, 0xf6);

	while (1) {
		int32_t driver_status = module_rfm69_get_status(rfm69);

		if (driver_status & RFM69_RSTATUS_TIMEOUT) {
			module_rfm69_mode_set(rfm69, RFM69_RMODE_SLEEP);
			rfm69->driver_state = RFM69_STATE_IDLE;

			return INTERFACE_NETDEV_RECEIVE_TIMEOUT;
		}
		if (driver_status & RFM69_RSTATUS_RXREADY) {
			break;
		}

		/* wait on semaphore otherwise */
		xSemaphoreTake(rfm69->semaphore, (TickType_t)10);
	}

	/* trigger fei measurement */
	module_rfm69_register_write(rfm69, 0x1e, 0x20);

	/* TODO: add upper margin (timeout) */
	while (1) {
		int32_t driver_status = module_rfm69_get_status(rfm69);


		if (driver_status & RFM69_RSTATUS_TIMEOUT) {
			module_rfm69_mode_set(rfm69, RFM69_RMODE_SLEEP);
			rfm69->driver_state = RFM69_STATE_IDLE;

			return INTERFACE_NETDEV_RECEIVE_TIMEOUT;
		}

		if (driver_status & RFM69_RSTATUS_FIFONOTEMPTY) {
			/* we don't know the length yet, read it */
			if (len == -1) {
				uint8_t plen;
				module_rfm69_fifo_read(rfm69, &plen, 1);
				len = plen;

				if (rfm69->rx_led != NULL) {
					interface_led_once(rfm69->rx_led, 0x11);
				}

				/* we need to reread the status again (to refresh possible fifolevel status set) */
				continue;
			}
		}

		/* if we have something to read */
		if ((len - data_pos) > 0) {
			/* if FIFO threshold is set, we can safely read at least 32 bytes */
			if (driver_status & RFM69_RSTATUS_FIFOLEVEL) {
				if ((len - data_pos) < 32) {
					/* something went wrong, we have 32B but we shouldn't read that much */
					module_rfm69_mode_set(rfm69, RFM69_RMODE_SLEEP);
					rfm69->driver_state = RFM69_STATE_IDLE;

					return INTERFACE_NETDEV_RECEIVE_FAILED;
				}

				data_pos += module_rfm69_fifo_read(rfm69, buf + data_pos, 32);
			}

			/* if payload ready is set, all packet data are in the fifo,
			 * read till the end */
			if (driver_status & RFM69_RSTATUS_PAYLOADREADY) {
				data_pos += module_rfm69_fifo_read(rfm69, buf + data_pos, len - data_pos);

				/* there should be no bytes left */
				if ((len - data_pos) > 0) {
					module_rfm69_mode_set(rfm69, RFM69_RMODE_SLEEP);
					rfm69->driver_state = RFM69_STATE_IDLE;

					return INTERFACE_NETDEV_RECEIVE_FAILED;
				}
				break;
			}

			/* if neither FIFO threshold is set nor payload is ready, check if we have
			 * at least 1 byte available in the FIFO and read it */
			if (driver_status & RFM69_RSTATUS_FIFONOTEMPTY) {
				data_pos += module_rfm69_fifo_read(rfm69, buf + data_pos, 1);
			}
		} else {
			/* we have no more bytes to read */
			break;
		}

		/* wait on semaphore */
		xSemaphoreTake(rfm69->semaphore, (TickType_t)10);
	}

	/* sample RSSI and FEI values */
	if (info != NULL) {
		info->rssi = -((int)(module_rfm69_register_read(rfm69, 0x24))) * 5;

		/* FEI value is 16bit 2's complement */
		int16_t fei = (int16_t)(module_rfm69_register_read(rfm69, 0x21) << 8 | module_rfm69_register_read(rfm69, 0x22));
		info->fei = (int32_t)(fei * 61);
	}

	module_rfm69_mode_set(rfm69, RFM69_RMODE_SLEEP);
	rfm69->driver_state = RFM69_STATE_IDLE;

	*buf_len = len;

	return INTERFACE_NETDEV_RECEIVE_OK;
}


static int32_t module_rfm69_start(void *context) {
	if (u_assert(context != NULL)) {
		return INTERFACE_NETDEV_START_FAILED;
	}
	struct module_rfm69 *rfm69 = (struct module_rfm69 *)context;

	if (rfm69->driver_state != RFM69_STATE_STOPPED) {
		return INTERFACE_NETDEV_START_FAILED;
	}

	rfm69->driver_state = RFM69_STATE_IDLE;
	return INTERFACE_NETDEV_START_OK;
}


static int32_t module_rfm69_stop(void *context) {
	if (u_assert(context != NULL)) {
		return INTERFACE_NETDEV_STOP_FAILED;
	}
	struct module_rfm69 *rfm69 = (struct module_rfm69 *)context;

	if (rfm69->driver_state != RFM69_STATE_IDLE) {
		return INTERFACE_NETDEV_STOP_FAILED;
	}

	rfm69->driver_state = RFM69_STATE_STOPPED;
	return INTERFACE_NETDEV_STOP_OK;
}


static uint32_t module_rfm69_get_capabilities(void *context) {
	if (u_assert(context != NULL)) {
		return 0;
	}
	return 0;
}


static int32_t module_rfm69_get_param(void *context, enum interface_netdev_param param, int32_t key, int32_t *value) {
	if (u_assert(context != NULL && value != NULL)) {
		return INTERFACE_NETDEV_GET_PARAM_FAILED;
	}
	struct module_rfm69 *rfm69 = (struct module_rfm69 *)context;
	(void)key;

	switch (param) {
		case INTERFACE_NETDEV_PARAM_FREQ:
			if (module_rfm69_get_frequency(rfm69, value) == MODULE_RFM69_GET_FREQUENCY_OK) {
				return INTERFACE_NETDEV_GET_PARAM_OK;
			}
			break;

		case INTERFACE_NETDEV_PARAM_TXPOWER:
			if (module_rfm69_get_txpower(rfm69, value) == MODULE_RFM69_GET_TXPOWER_OK) {
				return INTERFACE_NETDEV_GET_PARAM_OK;
			}
			break;

		default:
			return INTERFACE_NETDEV_GET_PARAM_UNSUPPORTED;
	}

	return INTERFACE_NETDEV_GET_PARAM_FAILED;
}


static int32_t module_rfm69_set_param(void *context, enum interface_netdev_param param, int32_t key, int32_t value) {
	if (u_assert(context != NULL)) {
		return INTERFACE_NETDEV_SET_PARAM_FAILED;
	}
	struct module_rfm69 *rfm69 = (struct module_rfm69 *)context;
	(void)key;

	switch (param) {
		case INTERFACE_NETDEV_PARAM_FREQ:
			if (module_rfm69_set_frequency(rfm69, value) == MODULE_RFM69_SET_FREQUENCY_OK) {
				return INTERFACE_NETDEV_SET_PARAM_OK;
			}
			break;

		case INTERFACE_NETDEV_PARAM_TXPOWER:
			if (module_rfm69_set_txpower(rfm69, value) == MODULE_RFM69_SET_TXPOWER_OK) {
				return INTERFACE_NETDEV_SET_PARAM_OK;
			}
			break;

		default:
			return INTERFACE_NETDEV_SET_PARAM_UNSUPPORTED;
	}

	return INTERFACE_NETDEV_SET_PARAM_FAILED;
}


int32_t module_rfm69_init(struct module_rfm69 *rfm69, const char *name, struct interface_spidev *spidev) {
	if (u_assert(rfm69 != NULL && name != NULL && spidev != NULL)) {
		return MODULE_RFM69_INIT_FAILED;
	}

	memset(rfm69, 0, sizeof(struct module_rfm69));
	hal_module_descriptor_init(&(rfm69->module), name);
	hal_module_descriptor_set_shm(&(rfm69->module), (void *)rfm69, sizeof(struct module_rfm69));

	/* Initialize network device interface. */
	interface_netdev_init(&(rfm69->iface));
	rfm69->iface.vmt.context = (void *)rfm69;
	rfm69->iface.vmt.send = module_rfm69_send;
	rfm69->iface.vmt.receive = module_rfm69_receive;
	rfm69->iface.vmt.start = module_rfm69_start;
	rfm69->iface.vmt.stop = module_rfm69_stop;
	rfm69->iface.vmt.get_capabilities = module_rfm69_get_capabilities;
	rfm69->iface.vmt.get_param = module_rfm69_get_param;
	rfm69->iface.vmt.set_param = module_rfm69_set_param;

	/* Init references to other modules. */
	rfm69->spidev = spidev;
	rfm69->tx_led = NULL;
	rfm69->rx_led = NULL;

	rfm69->semaphore = xSemaphoreCreateBinary();
	if (rfm69->semaphore == NULL) {
		goto err;
	}

	/* set packet mode, FSK modulation, BT=0.5 = 00000010 (0x02) */
	module_rfm69_register_write(rfm69, 0x02, 0x03);

	/* set bit rate = 50000bits/s */
	module_rfm69_register_write(rfm69, 0x03, 0x01); //02
	module_rfm69_register_write(rfm69, 0x04, 0x40); //80

	/* set fdev to 12500Hz */
	module_rfm69_register_write(rfm69, 0x05, 0x01); //0x00
	module_rfm69_register_write(rfm69, 0x06, 0x9a); //0xcd

	/* set lna input impedance and AGC */
	module_rfm69_register_write(rfm69, 0x18, 0x80); //80

	/* set receiver bandwidth to 100kHz single sideband */
	module_rfm69_register_write(rfm69, 0x19, 0xea); //2c
	module_rfm69_register_write(rfm69, 0x1a, 0xea);

	/* DIO mapping */
	module_rfm69_register_write(rfm69, 0x25, 0x80);
	module_rfm69_register_write(rfm69, 0x26, 0x57);

	/* set rx timeouts */
	module_rfm69_register_write(rfm69, 0x2a, 0x02); //02
	module_rfm69_register_write(rfm69, 0x2b, 0x80); //10

	/* set preamble size to 8 bytes */
	module_rfm69_register_write(rfm69, 0x2c, 0x00);
	module_rfm69_register_write(rfm69, 0x2d, 0x07);

	/* set rssi threshold to -116dBm */
	module_rfm69_register_write(rfm69, 0x29, 232);

	/* sync on, sync size 2B, 0 errors */
	module_rfm69_register_write(rfm69, 0x2e, 0x88); //88

	/* set sync as 0x72 0xa9 */
	module_rfm69_register_write(rfm69, 0x2f, 0x72);
	module_rfm69_register_write(rfm69, 0x30, 0xa9);

	/* variable length packet, no dc-free encoding, no crc, no address filtering */
	module_rfm69_register_write(rfm69, 0x37, 0x80);

	/* Maximum packet size. */
	module_rfm69_register_write(rfm69, 0x38, 0xff);

	/* set fifo threshold to 32 bytes */
	module_rfm69_register_write(rfm69, 0x3c, 0xa0);

	/* interpacket delay off, auto rx restart, aes off */
	module_rfm69_register_write(rfm69, 0x3d, 0xf2);

	/* turn on sensitivity boost */
	module_rfm69_register_write(rfm69, 0x58, 0x2d); //2d

	/* turn dagc on */
	module_rfm69_register_write(rfm69, 0x6f, 0x30); //30


	/* Set default driver state. */
	rfm69->driver_state = RFM69_STATE_IDLE;

	u_log(system_log, LOG_TYPE_INFO,
		"%s: RFM69CW/W/HW module driver initialized (defaults), reg3 = 0x%02x",
		rfm69->module.name,
		module_rfm69_register_read(rfm69, 0x03)
	);

	return MODULE_RFM69_INIT_OK;

err:
	u_log(system_log, LOG_TYPE_ERROR,
		"%s: error while initializing the RFM69CW/W/HW module driver",
		rfm69->module.name
	);

	return MODULE_RFM69_INIT_FAILED;
}


int32_t module_rfm69_free(struct module_rfm69 *rfm69) {
	if (u_assert(rfm69 != NULL && rfm69->driver_state != RFM69_STATE_UNINITIALIZED)) {
		return MODULE_RFM69_FREE_FAILED;
	}

	rfm69->driver_state = RFM69_STATE_UNINITIALIZED;

	return MODULE_RFM69_FREE_OK;
}


int32_t module_rfm69_interrupt_handler(struct module_rfm69 *rfm69) {
	if (u_assert(rfm69 != NULL && rfm69->driver_state != RFM69_STATE_UNINITIALIZED)) {
		return MODULE_RFM69_INTERRUPT_HANDLER_FAILED;
	}

	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	switch (rfm69->driver_state) {
		case RFM69_STATE_TRANSMITTING:
		case RFM69_STATE_RECEIVING:
			xSemaphoreGiveFromISR(rfm69->semaphore, &xHigherPriorityTaskWoken);
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			break;

		case RFM69_STATE_IDLE:
		default:
			break;
	};

	return MODULE_RFM69_INTERRUPT_HANDLER_OK;
}


int32_t module_rfm69_set_led(struct module_rfm69 *rfm69, enum module_rfm69_led_type type, struct interface_led *led) {
	if (u_assert(rfm69 != NULL && led != NULL)) {
		return MODULE_RFM69_SET_LED_FAILED;
	}

	switch (type) {
		case RFM69_LED_TYPE_ALL:
			rfm69->rx_led = led;
			rfm69->tx_led = led;
			break;

		case RFM69_LED_TYPE_TX:
			rfm69->tx_led = led;
			break;

		case RFM69_LED_TYPE_RX:
			rfm69->rx_led = led;
			break;

		default:
			return MODULE_RFM69_SET_LED_FAILED;
	}

	return MODULE_RFM69_SET_LED_FAILED;
}


int32_t module_rfm69_get_frequency(struct module_rfm69 *rfm69, int32_t *freq) {
	if (u_assert(rfm69 != NULL && freq != NULL)) {
		return MODULE_RFM69_GET_FREQUENCY_FAILED;
	}

	int32_t reg = 0;

	reg |= module_rfm69_register_read(rfm69, 0x07) << 16;
	reg |= module_rfm69_register_read(rfm69, 0x08) << 8;
	reg |= module_rfm69_register_read(rfm69, 0x09);

	*freq = (reg * 61) / 1000;

	return MODULE_RFM69_GET_FREQUENCY_OK;
}


int32_t module_rfm69_set_frequency(struct module_rfm69 *rfm69, int32_t freq) {
	if (u_assert(rfm69 != NULL)) {
		return MODULE_RFM69_SET_FREQUENCY_FAILED;
	}

	/* TODO: proper frequency checking */
	if (freq == 0) {
		return MODULE_RFM69_SET_FREQUENCY_FAILED;
	}

	/* frf register value = freq in Hz / 61Hz (one step) */
	int32_t reg = (freq * 1000) / 61;

	/* try to set the freq */
	module_rfm69_register_write(rfm69, 0x07, (reg >> 16) & 0xff);
	module_rfm69_register_write(rfm69, 0x08, (reg >> 8) & 0xff);
	module_rfm69_register_write(rfm69, 0x09, reg & 0xff);

	int32_t freq_r;
	module_rfm69_get_frequency(rfm69, &freq_r);

	/* TODO: fix possible rounding errors (may not be equal),
	 * tolerate +- 1kHz */
	if (abs(freq - freq_r) > 1) {
		return MODULE_RFM69_SET_FREQUENCY_FAILED;
	}

	return MODULE_RFM69_SET_FREQUENCY_OK;
}


int32_t module_rfm69_get_txpower(struct module_rfm69 *rfm69, int32_t *power) {
	if (u_assert(rfm69 != NULL && power != NULL)) {
		return MODULE_RFM69_GET_TXPOWER_FAILED;
	}

	int32_t reg = module_rfm69_register_read(rfm69, 0x11);
	*power = (reg & 0x1f) - 18;

	return MODULE_RFM69_GET_TXPOWER_OK;
}


int32_t module_rfm69_set_txpower(struct module_rfm69 *rfm69, int32_t power) {
	if (u_assert(rfm69 != NULL)) {
		return MODULE_RFM69_SET_TXPOWER_FAILED;
	}

	if (power < -18 || power > 13) {
		return MODULE_RFM69_SET_TXPOWER_FAILED;
	}

	module_rfm69_register_write(rfm69, 0x11, (uint8_t)(0x80 | ((power + 18) & 0x1f)));

	/* read txpower value back to check it */
	int32_t power_r;
	if (module_rfm69_get_txpower(rfm69, &power_r) != MODULE_RFM69_GET_TXPOWER_OK) {
		return MODULE_RFM69_SET_TXPOWER_FAILED;
	}

	if (power != power_r) {
		return MODULE_RFM69_SET_TXPOWER_FAILED;
	}

	return MODULE_RFM69_SET_TXPOWER_OK;
}


