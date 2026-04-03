/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * u-blox GPS driver service
 *
 * Copyright (c) 2021-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <i2c-bus.h>
#include "gps-ublox.h"

#define MODULE_NAME "gps-ublox"


gps_ublox_ret_t gps_ublox_set_i2c_transport(GpsUblox *self, I2cBus *i2c, uint8_t i2c_addr) {
	if (i2c == NULL) {
		return GPS_UBLOX_RET_FAILED;
	}
	self->i2c = i2c;
	self->i2c_addr = i2c_addr;
	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_set_uart_transport(GpsUblox *self, Stream *stream, Uart *uart) {
	if (uart == NULL) {
		return GPS_UBLOX_RET_FAILED;
	}
	self->uart = uart;
	self->stream = stream;
	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_set_uart_rtcm_transport(GpsUblox *self, Stream *stream, Uart *uart) {
	if (uart == NULL) {
		return GPS_UBLOX_RET_FAILED;
	}
	self->rtcm_uart = uart;
	self->rtcm_stream = stream;
	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_set_ubx_out_stream(GpsUblox *self, Stream *stream) {
	if (stream == NULL) {
		return GPS_UBLOX_RET_FAILED;
	}
	self->ubx_out_stream = stream;
	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_set_nmea_out_stream(GpsUblox *self, Stream *stream) {
	if (stream == NULL) {
		return GPS_UBLOX_RET_FAILED;
	}
	self->nmea_out_stream = stream;
	return GPS_UBLOX_RET_OK;
}


static void gps_ublox_rtcm_in_task(void *p) {
	GpsUblox *self = (GpsUblox *)p;

	while (true) {
		uint8_t buf[16];
		size_t len = 0;
		if (self->rtcm_in_stream->vmt->read_timeout(self->rtcm_in_stream, buf, sizeof(buf), &len, 1000) == STREAM_RET_OK) {
			/* Write RTCM directly to the main GNSS module UART stream.
			 * Do not check return values. We have to continue anyway. */
			/** @todo the data should be validated first */
			if (self->i2c) {
				self->i2c->vmt->transfer(self->i2c, self->i2c_addr, buf, len, NULL, 0);
			}
			if (self->stream) {
				self->stream->vmt->write(self->stream, buf, len);
			}
		}
	}

	vTaskDelete(NULL);
}


gps_ublox_ret_t gps_ublox_set_rtcm_in_stream(GpsUblox *self, Stream *stream) {
	if (stream == NULL) {
		return GPS_UBLOX_RET_FAILED;
	}
	self->rtcm_in_stream = stream;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("starting RTCM inptut processing"));
	xTaskCreate(gps_ublox_rtcm_in_task, "gps-ublox-rtcm", configMINIMAL_STACK_SIZE + 128, (void *)self, 3, &(self->rtcm_task));

	return GPS_UBLOX_RET_OK;
}


static gps_ublox_ret_t write_data(GpsUblox *self, const uint8_t *buf, size_t len) {
	if (self->i2c) {
		if (self->i2c->vmt->transfer(self->i2c, self->i2c_addr, buf, len, NULL, 0) == I2C_BUS_RET_OK) {
			return GPS_UBLOX_RET_OK;
		}
	}
	if (self->stream) {
		if (self->stream->vmt->write(self->stream, buf, len) == STREAM_RET_OK) {
			return GPS_UBLOX_RET_OK;
		}
	}
	return GPS_UBLOX_RET_FAILED;
}


static gps_ublox_ret_t read_data(GpsUblox *self, uint8_t *buf, size_t len) {
	uint32_t attempt = 0;
	if (self->i2c) {
		while (true) {
			/* Read number of bytes first. */
			uint8_t reg = 0xfd;
			uint16_t bytes = 0;
			self->i2c->vmt->transfer(self->i2c, self->i2c_addr, &reg, sizeof(reg), (uint8_t *)&bytes, sizeof(bytes));
			bytes = (bytes >> 8) | (bytes << 8);
			// if (bytes == 0) {
				// return GPS_UBLOX_RET_FAILED;
			// }
			// u_log(system_log, LOG_TYPE_DEBUG, "read_data len = %u, mam = %u", len, bytes);
			if (bytes < len) {
				/* Wait some time to fill the buffer. */
				attempt++;
				if (attempt > 100) {
					return GPS_UBLOX_RET_FAILED;
				}
				vTaskDelay(2);
				continue;
			}
			if (len > 0) {
				reg = 0xff;
				if (self->i2c->vmt->transfer(self->i2c, self->i2c_addr, &reg, sizeof(reg), buf, len) == I2C_BUS_RET_OK) {
					return GPS_UBLOX_RET_OK;
				}
			}
		}
	}
	if (self->stream) {
		size_t read = 0;
		while (len > 0) {
			/* Accept OK or TIMEOUT. */
			if (self->stream->vmt->read_timeout(self->stream, buf, len, &read, 2000) == STREAM_RET_OK) {
				len -= read;
				buf += read;
			} else {
				return GPS_UBLOX_RET_FAILED;
			}
		}
		return GPS_UBLOX_RET_OK;
	}
	return GPS_UBLOX_RET_FAILED;
}


static gps_ublox_ret_t send_ubx(GpsUblox *self, uint8_t class, uint8_t id, const uint8_t *buf, size_t len) {
	if (buf == NULL) {
		len = 0;
	}

	const uint8_t header[6] = {0xb5, 0x62, class, id, len & 0xff, (len << 8) & 0xff};
	//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("send_ubx cl = 0x%02x, id = 0x%02x, len = %u"), class, id, len);

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


static gps_ublox_ret_t eat_garbage_header(GpsUblox *self) {
	uint8_t byte = 0;
	uint32_t bytes_left = 200;
	while (byte != 0xb5) {
		if (read_data(self, &byte, sizeof(byte)) != GPS_UBLOX_RET_OK) {
			return GPS_UBLOX_RET_FAILED;
		}
		bytes_left--;
		if (bytes_left == 0) {
			/* Too much garbage or wrong protocol set. */
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("garbage at the input, wrong protocol?"));
			return GPS_UBLOX_RET_FAILED;
		}
	}
	/* 0xb5 found, check the second byte. */
	if (read_data(self, &byte, sizeof(byte)) != GPS_UBLOX_RET_OK) {
		return GPS_UBLOX_RET_FAILED;
	}
	if (byte != 0x62) {
		/* The second byte of the protocol header is garbage too. */
		return GPS_UBLOX_RET_FAILED;
	}
	/* A correct 0xb5, 0x62 header was found, we can safely continue parsing. */
	return GPS_UBLOX_RET_OK;
}


static gps_ublox_ret_t receive_ubx(GpsUblox *self, uint8_t *class, uint8_t *id, uint8_t *buf, size_t len, size_t *payload_len) {
	/* Try to find the beginning of the UBX command. If nothing is found in a reasonable timeout,
	 * return error. It means that there is probably no UBX command in the FIFO. */
	if (eat_garbage_header(self) != GPS_UBLOX_RET_OK) {
		return GPS_UBLOX_RET_FAILED;
	}

	uint8_t header[4] = {0};
	if (read_data(self, header, sizeof(header)) != GPS_UBLOX_RET_OK) {
		return GPS_UBLOX_RET_FAILED;
	}
	if (class) *class = header[0];
	if (id) *id = header[1];
	size_t plen = header[2] | (header[3] << 8);
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

	/* And eat the rest. */
	for (size_t i = len; i < plen; i++) {
		uint8_t dummy = 0;
		read_data(self, &dummy, sizeof(dummy));
	}

	/* Read checksum but do not check. */
	uint16_t checksum = 0;
	read_data(self, (uint8_t *)&checksum, sizeof(checksum));
	//u_log(system_log, LOG_TYPE_DEBUG, "ubx rx cl = 0x%02x, id = 0x%02x, len = %u", header[0], header[1], 6 + plen + 2);

	return GPS_UBLOX_RET_OK;
}


static gps_ublox_ret_t receive_ack(GpsUblox *self, uint8_t orig_class, uint8_t orig_id) {
	uint8_t class = 0;
	uint8_t id = 0;
	uint8_t data[2] = {0};
	size_t len = 0;

	uint32_t timeout = 100;
	while (receive_ubx(self, &class, &id, data, sizeof(data), &len) != GPS_UBLOX_RET_OK && timeout > 0) {
		/* Wait for the ACK a little bit more. */
		vTaskDelay(2);
		timeout--;
	}

	if (class == 0x05 && id == 0x01 && data[0] == orig_class && data[1] == orig_id) {
		return GPS_UBLOX_RET_OK;
	} else {
		return GPS_UBLOX_RET_FAILED;
	}
}


static gps_ublox_ret_t send_ubx_ack(GpsUblox *self, uint8_t class, uint8_t id, const uint8_t *buf, size_t len) {
	gps_ublox_ret_t ret = GPS_UBLOX_RET_FAILED;
	uint32_t attempts = 3;
	do {
		attempts--;
		if (attempts == 0) {
			u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("ACK error, ret = %d"), ret);
			return ret;
		}
		send_ubx(self, class, id, buf, len);
		ret = receive_ack(self, class, id);
	} while (ret != GPS_UBLOX_RET_OK);
	return GPS_UBLOX_RET_OK;
}


static gps_ublox_ret_t gps_ublox_set_ddc(GpsUblox *self, uint8_t addr, enum gps_ublox_proto proto_in, enum gps_ublox_proto proto_out) {
	/* Enable UBX output on DDC port instead of NMEA. */
	const uint8_t buf[] = {
		0x00, /* Port ID */
		0x00, /* Reserved */
		0x00, 0x00, /* Do not enable txReady */
		addr << 1, 0x00, 0x00, 0x00, /* I2C address (shifted 1 bit left) */
		0x00, 0x00, 0x00, 0x00, /* reserved */
		(uint8_t)proto_in, 0x00, /* Input protocol mask */
		(uint8_t)proto_out, 0x00, /* Output protocol mask */
		0x00, 0x00, /* Flags */
		0x00, 0x00, /* Reserved */
	};
	return send_ubx_ack(self, 0x06, 0x00, buf, sizeof(buf));
}


static gps_ublox_ret_t gps_ublox_set_ant(GpsUblox *self) {
	const uint8_t buf[] = {
		0x00, 0x00, /* Enable external antenna */
		0x00, 0x00, /* Do not reconfigure anything */
	};
	return send_ubx_ack(self, 0x06, 0x13, buf, sizeof(buf));
}


static gps_ublox_ret_t gps_ublox_set_periodic_msg(GpsUblox *self, uint8_t class, uint8_t id, uint8_t rate) {
	const uint8_t set_msg[] = {class, id, rate};
	return send_ubx_ack(self, 0x06, 0x01, set_msg, sizeof(set_msg));
}


static gps_ublox_ret_t gps_ublox_set_nav5(GpsUblox *self) {
	const uint8_t buf[] = {
		0x05, 0x00, /* Parameter bitmask, dyn mode + fix mode */
		0x02, /* Dyn mode stationary */
		0x01, /* Fix mode 2D only */
		0xd8, 0xd6, 0x00, 0x00, /* Altitude * 100 */
		0x00, 0x00, 0x00, 0x00, /* Alt var */
		0x00, /* Min elevation */
		0x00, /* Dr limit, reserved */
		0x00, 0x00, /* pDop */
		0x00, 0x00, /* tDop */
		0x00, 0x00, /* Position accuracy mask */
		0x00, 0x00, /* Time accuracy mask */
		0x00, /* Static hold threshold */
		0x00, /* DGNSS timeout */
		0x00, /* Num of satellites */
		0x00, /* Signal threshold */
		0x00, 0x00, /* Reserved */
		0x00, 0x00, /* Static hold distance */
		0x00, /* UTC standard */
		0x00, 0x00, 0x00, 0x00, 0x00, /* Reserved */

	};
	return send_ubx_ack(self, 0x06, 0x24, buf, sizeof(buf));
}


static gps_ublox_ret_t gps_ublox_set_timepulse(GpsUblox *self) {
	const uint8_t buf[] = {
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, /* Antenna cable delay */
		0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, /* Period */
		0x40, 0x42, 0x0f, 0x00, /* Period in lock 1s */
		0x00, 0x00, 0x00, 0x00, /* Pulse len */
		0x64, 0x00, 0x00, 0x00, /* Pulse len in lock is 100 us */
		0x00, 0x00, 0x00, 0x00, /* User configurable delay */
		0x77, 0x00, 0x00, 0x00, /* Flags. */
	};
	return send_ubx_ack(self, 0x06, 0x31, buf, sizeof(buf));
}


static void gps_ublox_nav_timeutc_to_timespec(struct timespec *timepulse, uint8_t *buf, size_t len, int32_t *accuracy) {
	struct tm ts = {0};

	ts.tm_hour = buf[16];
	ts.tm_min = buf[17];
	ts.tm_sec = buf[18];
	ts.tm_year = *(uint16_t *)&(buf[12]) - 1900;
	ts.tm_mon = buf[14] - 1;
	ts.tm_mday = buf[15];
	timepulse->tv_sec = mktime(&ts);
	timepulse->tv_nsec = 0;

	*accuracy = *(int32_t *)&(buf[4]);
}


gps_ublox_ret_t gps_ublox_cfg_valset(GpsUblox *self, uint32_t key, uint8_t *buf) {
	size_t value_sizes[8] = {0, 1, 1, 2, 4, 8, 0, 0};
	size_t value_size = value_sizes[(key & 0x70000000ull) >> 28];

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("valset %p = %d"), key, buf);

	uint8_t msg[8 + value_size] = {};

	msg[0] = 0x00; /* version */
	msg[1] = 0x01; /* update RAM layer only */
	msg[2] = 0x00;
	msg[3] = 0x00;
	msg[4] = key & 0xff;
	msg[5] = (key >> 8) & 0xff;
	msg[6] = (key >> 16) & 0xff;
	msg[7] = (key >> 24) & 0xff;
	memcpy(msg + 8, buf, value_size);

	return send_ubx_ack(self, 0x06, 0x8a, msg, 8 + value_size);
	vTaskDelay(10);
}


static gps_ublox_ret_t gps_ublox_rx_process(GpsUblox *self, uint8_t *buf, size_t len) {
	for (size_t i = 0; i < len; i++) {
		uint8_t b = buf[i];

		switch (self->rx_proto_state) {
			case GPS_UBLOX_PROTO_STATE_UBX_SYNC1:
				if (b == 0x62) {
					self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UBX_SYNC2;
				} else {
					self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UNKNOWN;
				}
				break;

			case GPS_UBLOX_PROTO_STATE_UBX_SYNC2:
				/* We definitely have a UBX message. Switch the state accordingly
				 * to start parsing it. Also, if configured, output the sync sequence
				 * to the UBX protocol output stream and enable outputting the whole message. */
				self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UBX_CLASS;

				if (self->ubx_out_stream) {
					self->ubx_out_stream->vmt->write(self->ubx_out_stream, (uint8_t []){0xb5, 0x62}, 2);
					self->ubx_out_state = true;
				}
				break;

			case GPS_UBLOX_PROTO_STATE_UBX_CLASS:
				self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UBX_ID;
				break;

			case GPS_UBLOX_PROTO_STATE_UBX_ID:
				self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UBX_LEN_LSB;
				self->ubx_out_plen = b;
				break;

			case GPS_UBLOX_PROTO_STATE_UBX_LEN_LSB:
				self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UBX_LEN_MSB;
				self->ubx_out_plen += b * 256;
				break;

			case GPS_UBLOX_PROTO_STATE_UBX_LEN_MSB:
				self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UBX_DATA;
				/* fallthrough */

			case GPS_UBLOX_PROTO_STATE_UBX_DATA:
				if (self->ubx_out_plen == 0) {
					self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UBX_CKA;
				} else {
					self->ubx_out_plen--;
				}
				break;

			case GPS_UBLOX_PROTO_STATE_UBX_CKA:
				/* The last state is CKA received, now we have CKB in the buffer. */
				self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UNKNOWN;
				if (self->ubx_out_stream) {
					self->ubx_out_stream->vmt->write(self->ubx_out_stream, &b, 1);
					self->ubx_out_state = false;
				}
				break;

			case GPS_UBLOX_PROTO_STATE_NMEA_START:
				if (b == 'G') {
					self->rx_proto_state = GPS_UBLOX_PROTO_STATE_NMEA_G;
					if (self->nmea_out_stream) {
						self->nmea_out_stream->vmt->write(self->nmea_out_stream, (uint8_t []){'$'}, 1);
					}
					self->nmea_out_state = true;
				}
				break;

			case GPS_UBLOX_PROTO_STATE_NMEA_G:
				if (b == '\r' || b == '\n') {
					self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UNKNOWN;
					self->nmea_out_state = false;
					if (self->nmea_out_stream) {
						self->nmea_out_stream->vmt->write(self->nmea_out_stream, (uint8_t []){'\r', '\n'}, 2);
					}
				}
				break;

			case GPS_UBLOX_PROTO_STATE_UNKNOWN:
			default:
				if (b == 0xb5) {
					self->rx_proto_state = GPS_UBLOX_PROTO_STATE_UBX_SYNC1;
				}
				if (b == '$') {
					self->rx_proto_state = GPS_UBLOX_PROTO_STATE_NMEA_START;
				}
		}

		/* Output raw data to per-protocol streams, if configured. */
		if (self->ubx_out_stream && self->ubx_out_state) {
			self->ubx_out_stream->vmt->write(self->ubx_out_stream, &b, 1);
		}
		if (self->nmea_out_stream && self->nmea_out_state) {
			self->nmea_out_stream->vmt->write(self->nmea_out_stream, &b, 1);
		}
	}

	return GPS_UBLOX_RET_OK;
}


static void gps_ublox_rx_task(void *p) {
	GpsUblox *self = (GpsUblox *)p;

	/* Configuration interface was changed considerably. This needs to be done
	 * after starting the service but prior to using it. */
	//gps_ublox_set_ddc(self, 0x42, GPS_UBLOX_PROTO_UBX, GPS_UBLOX_PROTO_UBX);
	/* Send message rate for UBX-NAV-TIMEUTC */
	//gps_ublox_set_periodic_msg(self, 0x01, 0x21, 1);
	//gps_ublox_set_timepulse(self);
	//gps_ublox_set_ant(self);
	//gps_ublox_set_nav5(self);

	/* Disable NMEA on UART1. */
	gps_ublox_cfg_valset(self, 0x10740002, (uint8_t[]){0x00});
	/* Enable NMEA GGA message. */
	//gps_ublox_cfg_valset(self, 0x209100bb, (uint8_t[]){0x01});

	/* Enable UBX SAT message output. */
	gps_ublox_cfg_valset(self, 0x20910016, (uint8_t[]){0x01});
	/* nav-sig */
	gps_ublox_cfg_valset(self, 0x20910346, (uint8_t[]){0x01});

	/* enable additional signals */
	gps_ublox_cfg_valset(self, 0x1031000a, (uint8_t[]){0x01});
	gps_ublox_cfg_valset(self, 0x1031000e, (uint8_t[]){0x01});
	gps_ublox_cfg_valset(self, 0x10310024, (uint8_t[]){0x01});
	gps_ublox_cfg_valset(self, 0x10310026, (uint8_t[]){0x01});

	/* enable RXM_COR */
	//gps_ublox_cfg_valset(self, 0x209106b7, (uint8_t[]){0x01});

	/* enable NAV_PVT */
	gps_ublox_cfg_valset(self, 0x20910007, (uint8_t[]){0x01});

	/* enable NAV_RELPOSNED */
	//gps_ublox_cfg_valset(self, 0x2091008e, (uint8_t[]){0x01});


	gps_ublox_cfg_valset(self, 0x20910034, (uint8_t[]){0x01});


	self->rx_running = true;
	while (self->rx_can_run) {
		/* Process a chunk of data at once. */
		uint8_t buf[16];
		size_t len = sizeof(buf);

		if (self->i2c) {
			/* Read number of bytes first. */
			uint16_t bytes = 0;
			self->i2c->vmt->transfer(self->i2c, self->i2c_addr, (uint8_t[]){0xfd}, 1, (uint8_t *)&bytes, sizeof(bytes));
			bytes = (bytes >> 8) | (bytes << 8);
			if (bytes == 0) {
				/* Nothing in the I2C output buffer. Wait a bit and poll again. */
				vTaskDelay(100);
				continue;
			}
			if (bytes > sizeof(buf)) {
				/* Limit the length to the available buffer space. */
				bytes = sizeof(buf);
			}
			if (self->i2c->vmt->transfer(self->i2c, self->i2c_addr, (uint8_t[]){0xff}, 1, buf, len) != I2C_BUS_RET_OK) {
				/* Something went wrong, do it again. */
				vTaskDelay(100);
				continue;
			}
			gps_ublox_rx_process(self, buf, len);
		}
		if (self->stream) {
			size_t read = 0;
			if (self->stream->vmt->read_timeout(self->stream, buf, sizeof(buf), &read, 100) == STREAM_RET_OK) {
				gps_ublox_rx_process(self, buf, read);
			}
		}



		//uint8_t class = 0;
		//uint8_t id = 0;
		//uint8_t data[32] = {0};
		//size_t len = 0;
		//receive_ubx(self, &class, &id, data, sizeof(data), &len);

		/* NAV_TIMEUTC message has arrived. 1PPS signal is already sampled now.
		 * Process the message and act accordingly. */
		//if (class == 0x01 && id == 0x21) {
			//UBaseType_t cs = taskENTER_CRITICAL_FROM_ISR();
			//gps_ublox_nav_timeutc_to_timespec(&self->timepulse_time, data, len, &self->timepulse_accuracy);
			//taskEXIT_CRITICAL_FROM_ISR(cs);
		//}
		//if (class == 0x01 && id == 0x14) {
			//struct ubx_nav_hpposllh hppos;
			//memcpy(&hppos, data, len);
			//u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("lat = %ld, lon = %ld, hacc = %lu, vacc = %lu"), hppos.lat, hppos.lon, hppos.hacc, hppos.vacc);
		//}
		//vTaskDelay(10);
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
	self->timepulse_count = 0;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("starting"));
	xTaskCreate(gps_ublox_rx_task, "gps-ublox-rx", configMINIMAL_STACK_SIZE + 512, (void *)self, 3, &(self->rx_task));

	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_stop(GpsUblox *self) {
	self->rx_can_run = false;
	while (self->rx_running) {
		vTaskDelay(10);
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopped"));

	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_timepulse_handler(GpsUblox *self) {
	/* Increment the current second counter NOW!
	 * If we waited for NAV_TIMEUTC, the time would be invalid until then. */
	UBaseType_t cs = taskENTER_CRITICAL_FROM_ISR();
	self->timepulse_time.tv_sec++;

	if (self->measure_clock && self->measure_clock->get) {
		self->measure_clock->get(self->measure_clock->parent, &self->measure_time);
	}
	self->timepulse_count++;
	taskEXIT_CRITICAL_FROM_ISR(cs);
	return GPS_UBLOX_RET_OK;
}


/*
	self->lptim_accumulator += self->lptim_adjust * (32768 + self->lptim_arr_adjust);
	self->lptim_arr_adjust = 0;
	if (self->lptim_accumulator > (3600 * 32768)) {
		self->lptim_arr_adjust = -1;
		self->lptim_accumulator -= 3600 * 32768;
	}
	if (self->lptim_accumulator < (-3600 * 32768)) {
		self->lptim_arr_adjust = 1;
		self->lptim_accumulator += 3600 * 32768;
	}
	lptimer_set_period(LPTIM1, 32768 + self->lptim_arr_adjust);
*/


gps_ublox_ret_t gps_ublox_init(GpsUblox *self) {
	memset(self, 0, sizeof(GpsUblox));

	return GPS_UBLOX_RET_OK;
}


gps_ublox_ret_t gps_ublox_free(GpsUblox *self) {
	(void)self;
	return GPS_UBLOX_RET_OK;
}



