/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nbus2 messaging bus implementation
 *
 * Copyright (c) 2023-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <main.h>
#include <interfaces/datagram.h>
#include "blake2s-siv.h"


#define NBUS_PBUF_COUNT 4
#define NBUS_PBUF_DATA_SIZE 1050
#define NBUS_SOCKET_COUNT 8
#define NBUS_SOCKET_RX_QUEUE_LEN 2

/** Key length in bytes for packet encryption. */
#define NBUS_PBUF_KE_LEN 16

/** Key length in bytes for packet MAC/SIV generation. */
#define NBUS_PBUF_KM_LEN 16

typedef enum {
	NBUS_RET_OK = 0,
	NBUS_RET_FAILED,
	NBUS_RET_BAD_PARAM,
	NBUS_RET_VOID,
	NBUS_RET_INVALID,
	NBUS_RET_BIG,
	NBUS_RET_TIMEOUT,
} nbus_ret_t;

/**
 * Data-link-layer cryptographic schemes used to protect nbus2 packets. The values are bit flags
 * so a receiver may accept several schemes at once (see struct nbus_config). Refer to nbus2.rst
 * for the on-wire details of each construction.
 */
enum nbus_crypto {
	/** ChaCha20 keystream + HalfSipHash MAC/SIV (the legacy firmware scheme). */
	NBUS_CRYPTO_CHACHA20_HALFSIPHASH = (1 << 0),
	/** BLAKE2s-only SIV-mode authenticated encryption (b2s_crypt + b2s_siv). */
	NBUS_CRYPTO_BLAKE2S_SIV = (1 << 1),
};

/**
 * Runtime configuration of an Nbus instance. A const instance is passed to nbus_init() which copies
 * it into the Nbus object; the caller's struct does not need to outlive the call.
 */
struct nbus_config {
	/** Datagram interface providing medium access and framing for whole nbus2 packets. */
	Datagram *dgram;

	/** A single cryptographic scheme used to protect transmitted packets. */
	enum nbus_crypto tx_crypto;

	/**
	 * Bitmask of cryptographic schemes accepted on receive. Multiple schemes may be enabled at
	 * once; every received packet is tried against each enabled scheme until one authenticates.
	 */
	uint32_t rx_crypto;
};

typedef struct nbus Nbus;
struct nbus_socket {
	bool used;
	bool enabled;
	Nbus *parent;
	Datagram datagram;

	QueueHandle_t rx_queue;

	uint8_t local_id[4];
	uint8_t local_id_mask[4];
	uint32_t local_ep;

	uint8_t remote_id[4];
	uint8_t remote_id_mask[4];
	uint32_t remote_ep;
};

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

typedef struct nbus {
	struct nbus_config config;
	const uint8_t *mac_key;
	size_t mac_key_len;

	SemaphoreHandle_t pbuf_lock;
	struct nbus_pbuf pbufs[NBUS_PBUF_COUNT];

	SemaphoreHandle_t socket_lock;
	struct nbus_socket sockets[NBUS_SOCKET_COUNT];

	/* Serializes the transmit path (sequence counter + encryption + medium write) as packets are
	 * sent directly in the context of the calling thread. */
	SemaphoreHandle_t tx_lock;

	TaskHandle_t rx_task;
	TaskHandle_t hk_task;

} Nbus;


nbus_ret_t nbus_init(Nbus *self, const struct nbus_config *config);
nbus_ret_t nbus_set_mac_key(Nbus *self, const uint8_t *mac_key, size_t mac_key_len);

struct nbus_pbuf *nbus_pbuf_allocate(Nbus *self);
nbus_ret_t nbus_pbuf_release(Nbus *self, struct nbus_pbuf *pbuf);
nbus_ret_t nbus_pbuf_set_destination(struct nbus_pbuf *self, const uint8_t id[4], uint8_t ep);
nbus_ret_t nbus_pbuf_set_source(struct nbus_pbuf *self, const uint8_t id[4], uint8_t ep);
nbus_ret_t nbus_pbuf_get_destination(struct nbus_pbuf *self, uint8_t id[4], uint8_t *ep);
nbus_ret_t nbus_pbuf_get_source(struct nbus_pbuf *self, uint8_t id[4], uint8_t *ep);

struct nbus_socket *nbus_socket_allocate(Nbus *self);
nbus_ret_t nbus_socket_release(Nbus *self, struct nbus_socket *socket);
nbus_ret_t nbus_socket_bind(struct nbus_socket *socket, const uint8_t *id, uint8_t ep);
nbus_ret_t nbus_socket_connect(struct nbus_socket *socket, const uint8_t *id, uint8_t ep);
