/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nbus2 protocol switch
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

#include <main.h>
#include <interfaces/datagram.h>


#define NBUS2_SWITCH_MAX_PORTS 4
#define NBUS2_SWITCH_MTU (1024 + 24)

typedef enum {
	NBUS2_SWITCH_RET_OK = 0,
	NBUS2_SWITCH_RET_FAILED,
} nbus2_switch_ret_t;


typedef struct nbus2_switch Nbus2Switch;

struct nbus2_switch_port {
	Datagram *datagram;
	Nbus2Switch *parent;
	TaskHandle_t receive_task;
	uint32_t tx_frames;
	uint32_t rx_frames;
	uint32_t rx_errors;
	uint32_t tx_errors;
	uint32_t rx_dropped;

	uint32_t locm3_led_port;
	uint32_t locm3_led_pin;
};


typedef struct nbus2_switch {
	struct nbus2_switch_port ports[NBUS2_SWITCH_MAX_PORTS];

	TaskHandle_t process_task;
	TaskHandle_t housekeeping_task;

} Nbus2Switch;


nbus2_switch_ret_t nbus2_switch_init(Nbus2Switch *self);
nbus2_switch_ret_t nbus2_switch_add_port(Nbus2Switch *self, Datagram *datagram, uint32_t locm3_led_port, uint32_t locm3_led_pin);
