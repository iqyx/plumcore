/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic HMI implementation
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <interfaces/datagram.h>
#include <interfaces/fb.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/event.h>
#include <interfaces/waveform-sink.h>
#include <interfaces/stream.h>
#include <services/nbus2/nbus2.h>


typedef enum {
	APP_RET_OK = 0,
	APP_RET_FAILED,
} app_ret_t;

#define PACKET_BUFFER_SIZE 1024
#define UPDATE_BUFFER_SIZE 1024

typedef struct {
	TaskHandle_t com_task;
	TaskHandle_t input_task;
	TaskHandle_t reader_task;

	Stream *reader;
	Fb *fb;
	I2cBus *i2c;
	Event *input;
	WaveformSink *speaker;

	/* nbus2 API */
	struct nbus_socket *socket;
	uint8_t packet_buffer[PACKET_BUFFER_SIZE];
	uint8_t update_buffer[UPDATE_BUFFER_SIZE];
} App;


app_ret_t app_init(App *self);
app_ret_t app_free(App *self);

