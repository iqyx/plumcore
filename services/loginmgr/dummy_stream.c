/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Null-sink stream — write-to-nowhere, read-returns-nothing
 *
 * Writes are accepted immediately and discarded. read() returns 0 bytes immediately
 * (deviates from the blocking contract, but prevents busy-wait on a source that never
 * produces data). read_timeout() always sleeps 1000 ms before returning STREAM_RET_TIMEOUT.
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <main.h>
#include <interfaces/stream.h>

#include "dummy_stream.h"

#define DUMMY_STREAM_READ_DELAY_MS 1000u


static stream_ret_t dummy_write(Stream *self, const void *buf, size_t size) {
	(void)self;
	(void)buf;
	(void)size;
	return STREAM_RET_OK;
}


static stream_ret_t dummy_read(Stream *self, void *buf, size_t size, size_t *read) {
	(void)self;
	(void)buf;
	(void)size;
	*read = 0;
	return STREAM_RET_OK;
}


static stream_ret_t dummy_write_timeout(Stream *self, const void *buf, size_t size, size_t *written, uint32_t timeout_ms) {
	(void)self;
	(void)buf;
	(void)timeout_ms;
	*written = size;
	return STREAM_RET_OK;
}


static stream_ret_t dummy_read_timeout(Stream *self, void *buf, size_t size, size_t *read, uint32_t timeout_ms) {
	(void)self;
	(void)buf;
	(void)size;
	(void)timeout_ms;
	vTaskDelay(pdMS_TO_TICKS(DUMMY_STREAM_READ_DELAY_MS));
	*read = 0;
	return STREAM_RET_TIMEOUT;
}


static const struct stream_vmt dummy_stream_vmt = {
	.write = dummy_write,
	.read = dummy_read,
	.write_timeout = dummy_write_timeout,
	.read_timeout = dummy_read_timeout,
};


dummy_stream_ret_t dummy_stream_init(DummyStream *self) {
	if (self == NULL) {
		return DUMMY_STREAM_RET_FAILED;
	}
	memset(self, 0, sizeof(DummyStream));
	self->iface.vmt = &dummy_stream_vmt;
	self->iface.parent = self;
	return DUMMY_STREAM_RET_OK;
}


dummy_stream_ret_t dummy_stream_free(DummyStream *self) {
	(void)self;
	return DUMMY_STREAM_RET_OK;
}
