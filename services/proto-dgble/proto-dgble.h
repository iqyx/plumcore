/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Tunnelling of datagrams over a BLE GATT service
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <message_buffer.h>

#include <interfaces/ble.h>
#include <interfaces/datagram.h>

/*
 * GATT layout
 * ===========
 * The service builds a single primary GATT service on a generic Ble device and tunnels one Datagram
 * interface per endpoint through it. All UUIDs are 128 bit, written most significant octet first (the
 * human-readable order), and share a common vendor prefix a3def8c0-acdd-a602-e3a1-78..:
 *
 *   service       a3def8c0-acdd-a602-e3a1-7890<sssssss>   the 96 bit prefix ...7890 followed by the
 *                                                          32 bit service ID passed at init
 *   endpoint N    a3def8c0-acdd-a602-e3a1-78920000<nnnn>  one characteristic per endpoint, the low 16 bit
 *                                                          carrying the endpoint number
 *   descriptor    a3def8c0-acdd-a602-e3a1-789100000000    a single read-only characteristic whose value
 *                                                          is a CBOR map describing the service and every
 *                                                          registered endpoint (see below)
 *
 * Each endpoint characteristic is readable, writable and notifiable. A peer write delivers an inbound
 * datagram (read side of the Datagram interface); a datagram write pushes an outbound notification to the
 * connected peer.
 *
 * The endpoint characteristics are created first (char indices 0..N-1) and the descriptor last: the
 * ST67W611 GATT server fails to route write events to any characteristic that follows a read-only one, so
 * the read-only descriptor must come after all the writable endpoints.
 *
 * Descriptor value
 * ================
 * The descriptor characteristic mirrors the nbus2 descriptor advertisement model: its value is a CBOR
 * map carrying the descriptor protocol version ("adv"), the service ID ("sid") and a nested map of
 * endpoints ("eps"), keyed by endpoint number, each holding the mandatory protocol name ("p") and the
 * optional protocol version ("pv"). It is encoded by proto_dgble_start().
 *
 * Lifecycle
 * =========
 *   1. proto_dgble_init()                  create the GATT service and take over the Ble event callback
 *   2. proto_dgble_add_endpoint() x N      register endpoints and obtain their Datagram interfaces
 *   3. proto_dgble_start()                 create the descriptor characteristic (last) and publish its
 *                                          value; must run before the GATT server is registered
 *   4. ble->vmt->server_start()            register the built GATT server with the device (owned by the
 *                                          caller, as the device may host other services)
 *
 * All endpoints must be added before proto_dgble_start(), and the whole service built before the GATT
 * server is registered, as the underlying device fixes the attribute table at registration time.
 */

typedef enum {
	PROTO_DGBLE_RET_OK = 0,
	PROTO_DGBLE_RET_FAILED,
	PROTO_DGBLE_RET_BAD_ARG,
	PROTO_DGBLE_RET_NOMEM,
} proto_dgble_ret_t;

/**
 * @brief Descriptor advertised for a single endpoint
 *
 * Emitted as the per-endpoint entry of the descriptor characteristic's CBOR map. Only the protocol name
 * is meaningful; unused optional keys are left NULL. Modelled on nbus_socket_descriptor.
 */
struct proto_dgble_descriptor {
	/** Protocol name emitted as the "p" key. NULL omits the endpoint's protocol. */
	const char *protocol;

	/** Protocol version emitted as the optional "pv" key. NULL omits the key. */
	const char *protocol_version;
};

/**
 * @brief Runtime configuration of a ProtoDgble instance
 *
 * A const instance is passed to proto_dgble_init() which copies it into the ProtoDgble object; the
 * caller's struct does not need to outlive the call.
 */
struct proto_dgble_config {
	/** Generic BLE device the GATT service is built on. */
	Ble *ble;

	/** 32 bit service ID appended to the 96 bit vendor prefix to form the service UUID. */
	uint32_t service_id;

	/**
	 * Optional BLE event callback. The service owns the device's single event callback (it needs the
	 * write events that carry inbound datagrams); every event is forwarded here so the caller can still
	 * observe connects, pairing and subscription changes. NULL disables forwarding.
	 */
	ble_event_cb event_cb;
	void *event_cb_ctx;
};

typedef struct proto_dgble ProtoDgble;

struct proto_dgble_endpoint {
	ProtoDgble *parent;
	bool used;
	uint16_t number;

	/** Advertised descriptor (pointers borrowed from the caller, must outlive the instance). */
	const char *protocol;
	const char *protocol_version;

	/** GATT characteristic backing this endpoint. */
	BleChar chr;

	/** Datagram interface handed to the caller. */
	Datagram dgram;

	/** Inbound datagrams received from peer writes, consumed by dgram read(). */
	MessageBufferHandle_t rx_buf;
};

struct proto_dgble {
	Ble *ble;
	uint32_t service_id;

	ble_event_cb user_event_cb;
	void *user_event_cb_ctx;

	/** GATT service and its read-only descriptor characteristic. */
	BleSrv srv;
	BleChar descriptor_chr;

	struct proto_dgble_endpoint endpoints[CONFIG_SERVICE_PROTO_DGBLE_MAX_ENDPOINTS];
};


proto_dgble_ret_t proto_dgble_init(ProtoDgble *self, const struct proto_dgble_config *config);
proto_dgble_ret_t proto_dgble_free(ProtoDgble *self);

/* Register an endpoint and obtain its Datagram interface. Must be called before the GATT server is
 * registered (ble->vmt->server_start). @p descriptor may be NULL. */
proto_dgble_ret_t proto_dgble_add_endpoint(ProtoDgble *self, uint16_t number,
                                           const struct proto_dgble_descriptor *descriptor, Datagram **dgram);

/* Create the descriptor characteristic (as the last characteristic) and publish its value. Call once,
 * after all endpoints have been added and before the caller registers the GATT server
 * (ble->vmt->server_start). */
proto_dgble_ret_t proto_dgble_start(ProtoDgble *self);
