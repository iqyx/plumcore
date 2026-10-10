/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * BLE GATT transport for RemoteInterface services
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Exports all RemoteInterface instances advertised in the service locator (ISERVICELOCATOR_TYPE_REMOTE_INTERFACE)
 * over a single primary GATT service. The conceptual documentation is in doc/services/remote-interface-ble.md.
 *
 * GATT layout
 * ===========
 * All UUIDs are 128 bit, written most significant octet first. The first 15 bytes are the first 15 bytes of
 * sha256("RemoteInterfaceBle"), the last one selects the attribute:
 *
 *   service   e146becd-ab61-d423-6586-22a4b435e700
 *   control   e146becd-ab61-d423-6586-22a4b435e701   one CBOR request per write, one response per notification
 *   data      e146becd-ab61-d423-6586-22a4b435e702   1 byte header + message fragment per write/notification
 *
 * Both characteristics are readable, writable and notifiable, none is read-only (the ST67W611 does not route
 * writes to a characteristic following a read-only one).
 *
 * Control protocol
 * ================
 * Requests are CBOR maps with a "c" key (command string), responses are CBOR maps holding only the result fields.
 * On error the response contains an "err" text string.
 *
 *  walk    n → {n}          next interface after n in the service locator order. n null (or absent) returns the
 *                           first one, the response after the last one is n null. The root interface ("") may
 *                           appear anywhere.
 *  desc    n → {p, pv, mtu, ms}
 *  open    n → {ret:"ok", ch}
 *  close   ch → {ret:"ok"}
 *
 * Data framing
 * ============
 * Every data value starts with a header byte: bit 7 START, bit 6 END, bits 5..0 channel (1..63). A message is sent
 * as consecutive fragments of a single channel, the first one with START, the last one with END. An empty message
 * (START | END, no payload) sent by the device means the session was closed by the interface. The channel stays
 * reserved until the central sends an explicit close command.
 *
 * Lifecycle
 * =========
 *   1. remote_interface_ble_init()     copy the configuration
 *   2. remote_interface_ble_start()    build the GATT service, take over the Ble event callback, start the task.
 *                                      The RemoteInterface instances must be advertised before, their largest
 *                                      MTU sizes the message buffers.
 *   3. ble->vmt->server_start()        register the GATT server (owned by the caller)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <message_buffer.h>

#include <interfaces/ble.h>
#include <interfaces/notify.h>
#include <interfaces/remote-interface.h>


typedef enum {
	REMOTE_INTERFACE_BLE_RET_OK = 0,
	REMOTE_INTERFACE_BLE_RET_FAILED,
	REMOTE_INTERFACE_BLE_RET_NULL,
	REMOTE_INTERFACE_BLE_RET_NOMEM,
} remote_interface_ble_ret_t;


struct remote_interface_ble_conf {
	/** Generic BLE device the GATT service is built on. */
	Ble *ble;

	/** Optional BLE event callback. The service owns the device's single event callback, every event is
	 *  forwarded here. NULL disables forwarding. */
	ble_event_cb event_cb;
	void *event_cb_ctx;
};


typedef struct remote_interface_ble RemoteInterfaceBle;

struct remote_interface_ble_channel {
	/** The channel is allocated by an open command. */
	bool used;

	/** Interface and its session, the session is NULL after it was closed by the interface (the channel is
	 *  reserved until the central closes it). */
	RemoteInterface *ri;
	void *session;
};

struct remote_interface_ble {
	struct remote_interface_ble_conf conf;

	/** GATT service and its characteristics. */
	BleSrv srv;
	BleChar control_chr;
	BleChar data_chr;

	/** Notify interface passed to every opened session, wakes up the task. */
	Notify notify;

	/** Values written by the central, filled by the event handler, consumed by the task. */
	MessageBufferHandle_t control_rx;
	MessageBufferHandle_t data_rx;

	/** Size of the message buffers below, the largest MTU of the interfaces advertised at start. */
	size_t msg_size;

	/** Reassembly of a message received from the central. */
	uint8_t *rx_msg;
	size_t rx_len;
	uint8_t rx_ch;
	bool rx_active;

	/** Message read from an interface, being sent to the central. */
	uint8_t *tx_msg;

	struct remote_interface_ble_channel channels[CONFIG_SERVICE_REMOTE_INTERFACE_BLE_CHANNELS];

	TaskHandle_t task;
};


/**
 * @brief Initialize the service
 *
 * @param self Preallocated instance
 * @param conf Service configuration, the structure is copied
 */
remote_interface_ble_ret_t remote_interface_ble_init(RemoteInterfaceBle *self, const struct remote_interface_ble_conf *conf);

/**
 * @brief Build the GATT service and start serving
 *
 * Must be called after the RemoteInterface instances are advertised and before the caller registers the GATT
 * server (ble->vmt->server_start). Interfaces advertised later are served too, unless their MTU is bigger than
 * the largest one seen here.
 */
remote_interface_ble_ret_t remote_interface_ble_start(RemoteInterfaceBle *self);

/**
 * @brief Stop the task, close all sessions and free the resources
 */
remote_interface_ble_ret_t remote_interface_ble_free(RemoteInterfaceBle *self);
