/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * u-blox GPS driver service
 *
 * Copyright (c) 2021-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/clock.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>


enum gps_ublox_mode {
	GPS_UBLOX_MODE_BACKUP,
	GPS_UBLOX_MODE_FULL,
};

typedef enum {
	GPS_UBLOX_RET_OK = 0,
	GPS_UBLOX_RET_FAILED = -1,
} gps_ublox_ret_t;

enum gps_ublox_proto {
	GPS_UBLOX_PROTO_UBX = 0x01,
	GPS_UBLOX_PROTO_NMEA = 0x02,
};

enum gps_ublox_proto_state {
	GPS_UBLOX_PROTO_STATE_UNKNOWN = 0,
	GPS_UBLOX_PROTO_STATE_UBX_SYNC1,
	GPS_UBLOX_PROTO_STATE_UBX_SYNC2,
	GPS_UBLOX_PROTO_STATE_UBX_CLASS,
	GPS_UBLOX_PROTO_STATE_UBX_ID,
	GPS_UBLOX_PROTO_STATE_UBX_LEN_LSB,
	GPS_UBLOX_PROTO_STATE_UBX_LEN_MSB,
	GPS_UBLOX_PROTO_STATE_UBX_DATA,
	GPS_UBLOX_PROTO_STATE_UBX_CKA,

	GPS_UBLOX_PROTO_STATE_NMEA_START,
	GPS_UBLOX_PROTO_STATE_NMEA_G,

};

struct ubx_nav_hpposllh {
	uint8_t version;
	uint8_t res1, res2;
	uint8_t flags;
	uint32_t itow;
	int32_t lon;
	int32_t lat;
	int32_t height;
	int32_t hmsl;
	int8_t lonhp;
	int8_t lathp;
	int8_t heighthp;
	int8_t hmslhp;
	uint32_t hacc;
	uint32_t vacc;
} __attribute__((packed));

typedef struct {

	/* Interface to the GNSS module itself. */
	I2cBus *i2c;
	uint8_t i2c_addr;
	Stream *stream;
	Uart *uart;
	Stream *rtcm_stream;
	Uart *rtcm_uart;

	/* Protocols can be output to different streams. */
	Stream *ubx_out_stream;
	Stream *nmea_out_stream;

	/* Aux variables for the protocol multiplexer. */
	bool ubx_out_state;
	size_t ubx_out_plen;
	bool nmea_out_state;

	/* RTCM protocol data input from a base station/network. */
	Stream *rtcm_in_stream;

	TaskHandle_t rtcm_task;

	TaskHandle_t rx_task;
	volatile bool rx_can_run;
	volatile bool rx_running;

	Clock *measure_clock;
	volatile struct timespec measure_time;

	volatile struct timespec timepulse_time;
	volatile int32_t timepulse_accuracy;
	volatile uint32_t timepulse_count;

	/* Protocol parser variables. */
	enum gps_ublox_proto_state rx_proto_state;

} GpsUblox;


gps_ublox_ret_t gps_ublox_set_i2c_transport(GpsUblox *self, I2cBus *i2c, uint8_t i2c_addr);
gps_ublox_ret_t gps_ublox_set_uart_transport(GpsUblox *self, Stream *stream, Uart *uart);
gps_ublox_ret_t gps_ublox_set_uart_rtcm_transport(GpsUblox *self, Stream *stream, Uart *uart);
gps_ublox_ret_t gps_ublox_set_ubx_out_stream(GpsUblox *self, Stream *stream);
gps_ublox_ret_t gps_ublox_set_nmea_out_stream(GpsUblox *self, Stream *stream);
gps_ublox_ret_t gps_ublox_set_rtcm_in_stream(GpsUblox *self, Stream *stream);
gps_ublox_ret_t gps_ublox_mode(GpsUblox *self, enum gps_ublox_mode mode);
gps_ublox_ret_t gps_ublox_start(GpsUblox *self);
gps_ublox_ret_t gps_ublox_stop(GpsUblox *self);
gps_ublox_ret_t gps_ublox_timepulse_handler(GpsUblox *self);
gps_ublox_ret_t gps_ublox_cfg_valset(GpsUblox *self, uint32_t key, uint8_t *buf);

gps_ublox_ret_t gps_ublox_init(GpsUblox *self);
gps_ublox_ret_t gps_ublox_free(GpsUblox *self);



