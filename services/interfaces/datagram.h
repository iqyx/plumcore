/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic bidirectional datagram interface
 *
 * Copyright (c) 2024-2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>

/**
 * @brief Datagram interface
 *
 * Datagram interface is basically identical to the Stream interface. Those two are different in
 * the way how framing is handled. Datagram interface always sends/receives the whole datagram
 * in a single method call.
 */

typedef enum datagram_ret {
	DATAGRAM_RET_OK = 0,
	DATAGRAM_RET_FAILED,
	DATAGRAM_RET_BAD_ARG,
	DATAGRAM_RET_TIMEOUT,
} datagram_ret_t;

/**
 * @brief Additional datagram metadata
 */
#define DATAGRAM_MSG_ADDR_SIZE_MAX 4
struct datagram_msg {
	size_t addr_size;
	uint8_t dst_addr[DATAGRAM_MSG_ADDR_SIZE_MAX];
	uint32_t dst_port;
	uint8_t src_addr[DATAGRAM_MSG_ADDR_SIZE_MAX];
	uint32_t src_port;
};

typedef struct datagram Datagram;
struct datagram_vmt {
	datagram_ret_t (*write)(Datagram *self, const void *buf, size_t len, const struct datagram_msg *msg);
	datagram_ret_t (*read)(Datagram *self, void *buf, size_t *len, struct datagram_msg *msg);

};

typedef struct datagram {
	const struct datagram_vmt *vmt;
	void *parent;
} Datagram;


