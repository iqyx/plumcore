/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Proxy exporting legacy Datagram protocol handlers as a RemoteInterface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @file
 *
 * Legacy protocol handlers (nbus-flash, nbus-mq-poll, ...) serve a single peer over a Datagram interface. The proxy
 * makes them available to RemoteInterface transports. It provides a single RemoteInterface (@c iface) and one
 * Datagram interface per session slot (@c sessions[i].dgram). The user creates one protocol handler instance per
 * session slot and wires it to the slot's Datagram, then advertises @c iface in the service locator.
 *
 * Messages written by the transport to a session are read by the handler from the session's Datagram, messages
 * written by the handler to the Datagram are read by the transport from the session. Both directions are buffered
 * in a message buffer holding up to @c queue_len messages of @c desc.mtu bytes.
 *
 * The handlers are unaware of the sessions. A handler wired to a free slot stays blocked in its Datagram read,
 * messages it writes while the slot is free are dropped. A legacy handler cannot close a session, therefore
 * REMOTE_INTERFACE_RET_CLOSED is never returned.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <main.h>
#include <message_buffer.h>

#include <interfaces/datagram.h>
#include <interfaces/remote-interface.h>
#include <interfaces/notify.h>


typedef enum {
	REMOTE_INTERFACE_PROXY_RET_OK = 0,
	REMOTE_INTERFACE_PROXY_RET_FAILED,
	REMOTE_INTERFACE_PROXY_RET_NULL,
	REMOTE_INTERFACE_PROXY_RET_BAD_ARG,
	REMOTE_INTERFACE_PROXY_RET_NOMEM,
} remote_interface_proxy_ret_t;


struct remote_interface_proxy_conf {
	/** Description of the exported interface. desc.max_sessions sets the number of session slots and Datagram
	 *  interfaces, desc.mtu the maximum message size in both directions. The strings are borrowed and must
	 *  outlive the instance. */
	struct remote_interface_desc desc;

	/** Number of messages buffered in each direction of a session. */
	size_t queue_len;
};


typedef struct remote_interface_proxy RemoteInterfaceProxy;

struct remote_interface_proxy_session {
	RemoteInterfaceProxy *parent;
	bool used;

	/** Notify interface passed to open(), NULL if the transport polls. */
	Notify *notify;

	/** Number of messages waiting in @c tx, passed to the Notify interface. */
	uint32_t tx_pending;

	/** Datagram interface the protocol handler serving this session slot is wired to. */
	Datagram dgram;

	/** Messages written by the transport, read by the protocol handler. */
	MessageBufferHandle_t rx;

	/** Messages written by the protocol handler, read by the transport. */
	MessageBufferHandle_t tx;
};

struct remote_interface_proxy {
	struct remote_interface_proxy_conf conf;

	/** RemoteInterface to be advertised in the service locator. */
	RemoteInterface iface;

	/** Protects the session slot allocation and the session state. */
	SemaphoreHandle_t lock;

	/** Counts free session slots, open() waits on it. */
	SemaphoreHandle_t free_slots;

	/** Array of desc.max_sessions session slots. */
	struct remote_interface_proxy_session *sessions;
};


/**
 * @brief Initialize the proxy
 *
 * Copies the configuration and allocates the session slots with their Datagram interfaces. Both @c iface and all
 * @c sessions[i].dgram are usable when the function returns.
 *
 * @param self Preallocated instance
 * @param conf Proxy configuration, the structure is copied
 *
 * @return REMOTE_INTERFACE_PROXY_RET_OK on success,
 *         REMOTE_INTERFACE_PROXY_RET_NULL on a NULL argument,
 *         REMOTE_INTERFACE_PROXY_RET_BAD_ARG on an invalid configuration,
 *         REMOTE_INTERFACE_PROXY_RET_NOMEM if the resources cannot be allocated.
 */
remote_interface_proxy_ret_t remote_interface_proxy_init(RemoteInterfaceProxy *self,
                                                         const struct remote_interface_proxy_conf *conf);

/**
 * @brief Free all resources of the proxy
 *
 * No transport nor protocol handler may use the proxy anymore.
 */
remote_interface_proxy_ret_t remote_interface_proxy_free(RemoteInterfaceProxy *self);
