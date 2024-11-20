/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Generic bidirectional datagram interface
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>

/** @todo Datagram interface is basically identical to the Stream interface. Those two are different in
 *        the way how framing is handled. Datagram interface always sends/receives the whole datagram
 *        in a single method call.
 */

typedef enum datagram_ret {
	DATAGRAM_RET_OK = 0,
	DATAGRAM_RET_FAILED,
	DATAGRAM_RET_TIMEOUT,
	DATAGRAM_RET_EOF,
} datagram_ret_t;

typedef struct datagram Datagram;
struct datagram_vmt {
	stream_ret_t (*write)(Stream *self, const void *buf, size_t size);
	stream_ret_t (*read)(Stream *self, void *buf, size_t size, size_t *read);
	stream_ret_t (*write_timeout)(Stream *self, const void *buf, size_t size, size_t *written, uint32_t timeout_ms);
	stream_ret_t (*read_timeout)(Stream *self, void *buf, size_t size, size_t *read, uint32_t timeout_ms);
};

typedef struct datagram {
	const struct datagram_vmt *vmt;
	void *parent;
} Datagram;


