/* SPDX-License-Identifier: BSD-2-Clause
 *
 * CAN interface
 *
 * Copyright (c) 2018-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>

typedef enum {
	CAN_RET_OK = 0,
	CAN_RET_FAILED,
	CAN_RET_BUS_OFF,
} can_ret_t;

struct can_message {
	bool rtr;
	bool extid;
	uint32_t id;
	size_t len;
	/* Contains both classic CAN and CAN-FD messages. */
	uint8_t buf[64];
	uint32_t timestamp;
};

typedef struct can Can;

struct can_vmt {
	can_ret_t (*send)(Can *self, const struct can_message *msg, uint32_t timeout_ms);
	can_ret_t (*receive)(Can *self, struct can_message *msg, uint32_t timeout_ms);
};

typedef struct can {
	const struct can_vmt *vmt;
	void *parent;
} Can;





