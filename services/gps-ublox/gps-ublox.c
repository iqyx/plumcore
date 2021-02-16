/* SPDX-License-Identifier: BSD-2-Clause
 *
 * u-blox GPS driver service
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <i2c-bus.h>
#include "gps-ublox.h"

/* Disciplining LPTIM directly. This should be moved to a separate
 * service in order to not repeat the same functionality while
 * eg. using a different GNSS receiver. */
#include <libopencm3/stm32/lptimer.h>

#define MODULE_NAME "gps-ublox"


gps_ublox_ret_t gps_ublox_set_i2c_transport(GpsUblox *self, I2cBus *i2c, uint8_t i2c_addr) {
	if (i2c == NULL) {
		return GPS_UBLOX_RET_FAILED;
	}
	self->i2c = i2c;
	self->i2c_addr = i2c_addr;
	return GPS_UBLOX_RET_OK;
}


static gps_ublox_ret_t write_data(GpsUblox *self, const uint8_t *buf, size_t len) {
	if (self->i2c) {
		if (self->i2c->transfer(self->i2c->parent, self->i2c_addr, buf, len, NULL, 0) == I2C_BUS_RET_OK) {
			return GPS_UBLOX_RET_OK;
		}
	}
	return GPS_UBLOX_RET_FAILED;
}


static gps_ublox_ret_t read_data(GpsUblox *self, uint8_t *buf, size_t len) {
	if (self->i2c) {
		while (true) {
			/* Read number of bytes first. */
			uint8_t reg = 0xfd;
			uint16_t bytes = 0;
			self->i2c->transfer(self->i2c->parent, self->i2c_addr, &reg, sizeof(reg), (uint8_t *)&bytes, sizeof(bytes));
			bytes = (bytes >> 8) | (bytes << 8);
			//u_log(system_log, LOG_TYPE_DEBUG, "read_data len = %u, mam = %u", len, bytes);
			if (bytes < len) {
				/* Wait some time to fill the buffer. */
				vTaskDelay(2);
				continue;
			}
			if (len > bytes) {
				len = bytes;
			}
			if (len > 0) {
				reg = 0xff;
				if (self->i2c->transfer(self->i2c->parent, self->i2c_addr, &reg, sizeof(reg), buf, len) == I2C_BUS_RET_OK) {
					return GPS_UBLOX_RET_OK;
				}
			}
		}
	}
	return GPS_UBLOX_RET_FAILED;
}


static gps_ublox_ret_t send_ubx(GpsUblox *self, uint8_t class, uint8_t id, const uint8_t *buf, size_t len) {
	if (buf == NULL) {
		len = 0;
	}

	const uint8_t header[6] = {0xb5, 0x62, class, id, len & 0xff, (len << 8) & 0xff};

	uint8_t cka = 0;
	uint8_t ckb = 0;
	for (size_t i = 2; i < 6; i++) {
		cka += header[i];
		ckb += cka;
	}
	for (size_t i = 0; i < len; i++) {
		cka += buf[i];
		ckb += cka;
	}
	uint8_t checksum[2] = {cka, ckb};

	write_data(self, header, sizeof(header));
	if (len > 0) {
		write_data(self, buf, len);
	}
	write_data(self, checksum, sizeof(checksum));
	return GPS_UBLOX_RET_OK;
}


static gps_ublox_ret_t receive_ubx(GpsUblox *self, uint8_t *class, uint8_t *id, uint8_t *buf, size_t len, size_t *payload_len) {
	/* Rea the fixed length header first. */
	uint8_t header[6] = {0};
	read_data(self, header, sizeof(header));
	if (header[0] != 0xb5 || header[1] != 0x62) {
		/* Garbage. There is nothing to do. */
		return GPS_UBLOX_RET_FAILED;
	}
	if (class) *class = header[2];
	if (id) *id = header[3];
	size_t plen = header[4] | (header[5] << 8);
	if (payload_len) *payload_len = plen;
	if (plen < len) {
		len = plen;
	}
	if (buf == NULL) {
		len = 0;
	}

	/* Read only payload_len bytes and only if it fits inside buf.
	 * If the buf is smaller than payload_len, read only len bytes. */
	if (len > 0) {
		read_data(self, buf, len);
	}

	/* Eat the rest. */
	for (size_t i = len; i < plen; i++) {
		uint8_t dummy = 0;
		read_data(self, &dummy, sizeof(dummy));
	}

	/* Read checksum but do not check. */
	uint16_t checksum = 0;
	read_data(self, (uint8_t *)&checksum, sizeof(checksum));
	//u_log(system_log, LOG_TYPE_DEBUG, "ubx rx 0x%02x 0x%02x, cl = 0x%02x, id = 0x%02x, len = %u", header[0], header[1], header[2], header[3], 6 + plen + 2);

	return GPS_UBLOX_RET_OK;
}


static void gps_ublox_rx_task(void *p) {
	GpsUblox *self = (GpsUblox *)p;

	vTaskDelay(100);
	self->rx_running = true;

	/** @todo init here */
	/* Enable UBX output on DDC port instead of NMEA. */
	const uint8_t buf[] = {
		0x00, /* Port ID */
		0x00, /* Reserved */
		0x00, 0x00, /* Do not enable txReady */
		0x84, 0x00, 0x00, 0x00, /* Keep 0x42 as the I2C address */
		0x00, 0x00, 0x00, 0x00, /* reserved */
		0x01, 0x00, /* Input protocol mask, UBX only */
		0x01, 0x00, /* Output protocol mask, UBX only */
		0x00, 0x00, /* Flags */
		0x00, 0x00, /* Reserved */
	};
	send_ubx(self, 0x06, 0x00, buf, sizeof(buf));

	/* Send message rate for UBX-NAV-TIMEUTC */
	const uint8_t set_time[] = {
		0x01, /* Message class */
		0x21, /* Message ID */
		0x01,
	};
	send_ubx(self, 0x06, 0x01, set_time, sizeof(set_time));

	/* Configure timepulse (1PPS) output. */
	const uint8_t set_timepulse[] = {
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, /* Antenna cable delay */
		0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, /* Period */
		0x40, 0x42, 0x0f, 0x00, /* Period in lock 1s */
		0x00, 0x00, 0x00, 0x00, /* Pulse len */
		0xdc, 0x41, 0x0f, 0x00, /* Pulse len in lock is 100 us */
		0x00, 0x00, 0x00, 0x00, /* User configurable delay */
		0x37, 0x00, 0x00, 0x00, /* Flags. Falling edge at top of second! */
	};
	send_ubx(self, 0x06, 0x31, set_timepulse, sizeof(set_timepulse));

	while (self->rx_can_run) {
		uint8_t class = 0;
		uint8_t id = 0;
		uint8_t data[32] = {0};
		size_t len = 0;
		receive_ubx(self, &class, &id, data, sizeof(data), &len);
		if (class == 0x01 && id == 0x21) {
			u_log(system_log, LOG_TYPE_DEBUG, "nav_timeutc sec = %d, accuracy = %lu ns", data[18], *(uint32_t *)&(data[4]));
			u_log(system_log, LOG_TYPE_DEBUG, "lptim_last = %u", self->lptim_last);
		}
		vTaskDelay(10);
	}
	self->rx_running = false;
	vTaskDelete(NULL);
}



gps_ublox_ret_t gps_ublox_mode(GpsUblox *self, enum gps_ublox_mode mode) {
	switch (mode) {
		case GPS_UBLOX_MODE_BACKUP: {
			uint8_t backup[] = {0x00, 0x00, 0x80, 0x00, 0x02, 0x00, 0x00, 0x00};
			send_ubx(self, 0x02, 0x41, backup, sizeof(backup));
			break;
		}
		default:
			return GPS_UBLOX_RET_FAILED;
	}
	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_start(GpsUblox *self) {
	self->rx_can_run = true;
	xTaskCreate(gps_ublox_rx_task, "gps-ublox-rx", configMINIMAL_STACK_SIZE + 384, (void *)self, 1, &(self->rx_task));

	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_stop(GpsUblox *self) {
	(void)self;

	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_timepulse_handler(GpsUblox *self) {
	/* Not the best approach, we should be using input capture mode.
	 * LPTIM does not have any, we are using an external interrupt instead.
	 * Try to make processing deterministic. */
	uint16_t lptim_now = lptimer_get_counter(LPTIM1);
	self->lptim_last = lptim_now;
	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_init(GpsUblox *self) {
	memset(self, 0, sizeof(GpsUblox));

	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_free(GpsUblox *self) {
	(void)self;
	return GPS_UBLOX_RET_OK;
}



