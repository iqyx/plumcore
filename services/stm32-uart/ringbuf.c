/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Lock-free single producer, single consumer byte ring buffer
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stddef.h>

#include "ringbuf.h"


ringbuf_ret_t ringbuf_init(struct ringbuf *self, uint8_t *buf, size_t size) {
	self->buf = buf;
	self->size = size;
	self->head = 0;
	self->tail = 0;

	return RINGBUF_RET_OK;
}


ringbuf_ret_t ringbuf_put(struct ringbuf *self, const uint8_t *data, size_t len) {
	size_t used = 0;
	ringbuf_used(self, &used);
	if (len > self->size - 1 - used) {
		return RINGBUF_RET_FULL;
	}

	size_t head = self->head;
	for (size_t i = 0; i < len; i++) {
		self->buf[head] = data[i];
		head++;
		if (head == self->size) {
			head = 0;
		}
	}

	/* Publish the new head only after the data is stored. */
	__atomic_thread_fence(__ATOMIC_RELEASE);
	self->head = head;

	return RINGBUF_RET_OK;
}


ringbuf_ret_t ringbuf_write(struct ringbuf *self, const uint8_t *data, size_t len, size_t *written) {
	size_t used = 0;
	ringbuf_used(self, &used);
	size_t free = self->size - 1 - used;
	if (len > free) {
		len = free;
	}
	*written = len;

	return ringbuf_put(self, data, len);
}


ringbuf_ret_t ringbuf_get(struct ringbuf *self, uint8_t *b) {
	size_t tail = self->tail;
	if (tail == self->head) {
		return RINGBUF_RET_EMPTY;
	}

	/* Read the data only after observing the head which published it. */
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	*b = self->buf[tail];
	tail++;
	if (tail == self->size) {
		tail = 0;
	}

	/* Release the slot only after the byte is read out. */
	__atomic_thread_fence(__ATOMIC_RELEASE);
	self->tail = tail;

	return RINGBUF_RET_OK;
}


ringbuf_ret_t ringbuf_used(struct ringbuf *self, size_t *used) {
	size_t head = self->head;
	size_t tail = self->tail;
	*used = (head >= tail) ? (head - tail) : (head + self->size - tail);

	return RINGBUF_RET_OK;
}
