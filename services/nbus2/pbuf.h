/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nbus2 messaging bus implementation
 *
 * Copyright (c) 2023-2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <main.h>


/**
 * Key length in bytes for packet encryption.
 */
#define NBUS_PBUF_KE_LEN 16

/**
 * Key length in bytes for packet MAC/SIV generation.
 */
#define NBUS_PBUF_KM_LEN 16


enum nbus_pbuf_state {
	/**
	 * Packet buffer resides in the pool of free buffers usually owned
	 * by the service implementing the interface. The pool is the sole
	 * owner of the buffer. No operations nor access are possible.
	 */
	NBUS_PBUF_STATE_EMPTY = 0,

	/**
	 * Packet buffer was returned by the allocator factory function/method.
	 * Now the owner is whoever received the returned pointer. After using
	 * the buffer, it must be manually freed.
	 */
	NBUS_PBUF_STATE_ALLOCATED,

	/**
	 * The buffer may not be freed immediately. Wait for garbage collection.
	 */
	NBUS_PBUF_STATE_GC,
};


struct nbus_pbuf {
	enum nbus_pbuf_state state;

	uint8_t ke[NBUS_PBUF_KE_LEN];
	uint8_t km[NBUS_PBUF_KM_LEN];

	/**
	 * Preallocated packet/datagram buffer. More complex allocators may
	 * allocate the requested number of bytes.
	 */
	uint8_t *buf;

	/**
	 * The allocated buffer size in bytes.
	 */
	size_t buf_size;

	/**
	 * Length of the packet in the buffer (that is, number of bytes actually used.
	 */
	size_t buf_len;

	/**
	 * Semaphore handle to allow waiting on the packet buffer (when receiving
	 * and sending packets/datagrams)
	 */
	SemaphoreHandle_t s;

};
