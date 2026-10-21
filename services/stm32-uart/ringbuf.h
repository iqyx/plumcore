/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lock-free single producer, single consumer byte ring buffer
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>


typedef enum {
	RINGBUF_RET_OK = 0,
	RINGBUF_RET_FULL,
	RINGBUF_RET_EMPTY,
} ringbuf_ret_t;

/* The producer (e.g. an ISR) only advances head, the consumer (a single task) only advances tail, so no locking is
 * required. One slot is always kept free to tell a full buffer from an empty one. */
struct ringbuf {
	uint8_t *buf;
	size_t size;
	volatile size_t head;
	volatile size_t tail;
};


ringbuf_ret_t ringbuf_init(struct ringbuf *self, uint8_t *buf, size_t size);

/* Store all @p len bytes or nothing. The bytes become visible to the consumer at once. */
ringbuf_ret_t ringbuf_put(struct ringbuf *self, const uint8_t *data, size_t len);

/* Store as many of the @p len bytes as fit, their number is returned in @p written. */
ringbuf_ret_t ringbuf_write(struct ringbuf *self, const uint8_t *data, size_t len, size_t *written);

/* Take a single byte. */
ringbuf_ret_t ringbuf_get(struct ringbuf *self, uint8_t *b);

/* Number of bytes stored. */
ringbuf_ret_t ringbuf_used(struct ringbuf *self, size_t *used);
