/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus switch
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <main.h>
#include <interfaces/can.h>
#include <services/nbus/nbus.h>


#define NBUS_SWITCH_MAX_CHANNELS 256
#define NBUS_SWITCH_MAX_PORTS 4
#define NBUS_SWITCH_IQ_SIZE 128
#define NBUS_SWITCH_MAX_LIFETIME 10

typedef enum {
	NBUS_SWITCH_RET_OK = 0,
	NBUS_SWITCH_RET_FAILED,
} nbus_switch_ret_t;

typedef uint16_t channel_t;
typedef uint8_t port_t;

typedef struct nbus_switch NbusSwitch;

struct nbus_switch_port {
	Can *can;
	NbusSwitch *parent;
	TaskHandle_t receive_task;
	uint32_t tx_frames;
	uint32_t rx_frames;
	uint32_t rx_errors;
	uint32_t tx_errors;
	uint32_t rx_dropped;
};

struct __attribute__((__packed__)) nbus_switch_channel {
	channel_t ch;
	enum nbus_direction dir;
	struct nbus_switch_port *port;
	uint32_t frames;
	uint32_t last_access;
};


struct nbus_switch_iq_item {
	struct nbus_switch_port *port;
	struct can_message msg;
};


typedef struct nbus_switch {

	/* Channel lookup table */
	struct nbus_switch_channel ch[NBUS_SWITCH_MAX_CHANNELS];

	struct nbus_switch_port ports[NBUS_SWITCH_MAX_PORTS];

	QueueHandle_t iq;
	TaskHandle_t process_task;
	TaskHandle_t housekeeping_task;
} NbusSwitch;


nbus_switch_ret_t nbus_switch_init(NbusSwitch *self);
nbus_switch_ret_t nbus_switch_add_port(NbusSwitch *self, Can *can);
