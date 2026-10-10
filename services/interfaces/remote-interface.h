/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic interface for functionality exported to remote peers
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * A RemoteInterface is a transport agnostic piece of functionality used by a remote peer (a flash updater, a message
 * queue poller, ...). A transport opens sessions on it, pushes received messages with write() and pulls messages to
 * send with read(). The conceptual documentation is in doc/interfaces/remote-interface.md.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <interfaces/notify.h>


typedef enum {
	/** The function performed as intended with no unexpected result. */
	REMOTE_INTERFACE_RET_OK = 0,

	/** A generic failure not covered by a more specific code. */
	REMOTE_INTERFACE_RET_FAILED,

	/** A NULL pointer was given as an instance or a required argument. */
	REMOTE_INTERFACE_RET_NULL,

	/** A wrong argument value was used (an unknown session, a message bigger than the MTU, a read buffer
	 *  smaller than the MTU). */
	REMOTE_INTERFACE_RET_BAD_ARG,

	/** The session was closed by the interface. The transport must notify the peer and call close() to
	 *  free the session slot. */
	REMOTE_INTERFACE_RET_CLOSED,

	/** The operation did not complete within the given timeout (no slot was free or the interface was locked
	 *  in open(), a message was not accepted by write(), no message was available for read()). */
	REMOTE_INTERFACE_RET_TIMEOUT,
} remote_interface_ret_t;


/**
 * Structured description of an interface. It is static for the whole lifetime of the interface and owned by
 * it. A transport encodes it in its own format, mapping the fields to the transport-specific keys (eg. the
 * "p" and "pv" keys of the nbus2 descriptor advertisement).
 */
struct remote_interface_desc {
	/** Name of the protocol spoken over the interface (eg. "nbus-flash", "mq-poll"). Mandatory. */
	const char *protocol;

	/** Version of the protocol. NULL if not versioned. */
	const char *protocol_version;

	/** Maximum size of a single written or read message in bytes. */
	size_t mtu;

	/** Maximum number of concurrently open sessions. */
	uint8_t max_sessions;

	/** Inactivity timeout after which a connectionless transport closes a session. Zero if the session
	 *  should be closed only when the transport loses the peer. */
	uint32_t session_idle_ms;
};


typedef struct remote_interface RemoteInterface;

struct remote_interface_vmt {
	/**
	 * @brief Open a new session
	 *
	 * Allocate a session slot and initialise the per-session protocol state.
	 *
	 * @param self Interface instance
	 * @param notify Notify interface called when a message is queued for reading or the session is closed by
	 *               the interface, the value is the number of messages waiting to be read. NULL if the
	 *               transport polls read() on its own.
	 * @param session The opaque session pointer is returned here
	 * @param timeout_ms Time to wait for a free slot or for a lock to be released, 0 to not wait at all
	 *
	 * @return REMOTE_INTERFACE_RET_OK on success,
	 *         REMOTE_INTERFACE_RET_TIMEOUT if no slot was free or the interface was locked for the whole
	 *         @p timeout_ms,
	 *         REMOTE_INTERFACE_RET_FAILED if the session is refused for another reason.
	 */
	remote_interface_ret_t (*open)(RemoteInterface *self, Notify *notify, void **session, uint32_t timeout_ms);

	/**
	 * @brief Close a session
	 *
	 * Abort unfinished operations, release any lock held by the session, drop all pending messages and free
	 * the session slot. Called by the transport when the peer disconnects or the session times out, and
	 * after REMOTE_INTERFACE_RET_CLOSED was returned for a session closed by the interface. Must be called
	 * exactly once for every opened session and never concurrently with another call on the same session,
	 * the session pointer is invalid afterwards.
	 *
	 * @param self Interface instance
	 * @param session Session to close
	 *
	 * @return REMOTE_INTERFACE_RET_OK on success,
	 *         REMOTE_INTERFACE_RET_NULL or REMOTE_INTERFACE_RET_BAD_ARG on an invalid session.
	 */
	remote_interface_ret_t (*close)(RemoteInterface *self, void *session);

	/**
	 * @brief Push a message received from the remote peer to the interface
	 *
	 * The message is either processed synchronously or queued for processing. The buffer is owned by the
	 * transport and may be reused as soon as the method returns.
	 *
	 * @param self Interface instance
	 * @param session Session the message belongs to
	 * @param buf Message received from the peer
	 * @param len Message length, at most desc->mtu
	 * @param timeout_ms Time to wait for the interface to accept the message, 0 to not wait at all
	 *
	 * @return REMOTE_INTERFACE_RET_OK if the message was accepted,
	 *         REMOTE_INTERFACE_RET_TIMEOUT if it was not accepted in time (the transport drops it),
	 *         REMOTE_INTERFACE_RET_CLOSED if the session was closed by the interface,
	 *         REMOTE_INTERFACE_RET_BAD_ARG on an unknown session or an oversized message.
	 */
	remote_interface_ret_t (*write)(RemoteInterface *self, void *session, const void *buf, size_t len,
	                                uint32_t timeout_ms);

	/**
	 * @brief Pull a message to be sent to the remote peer from the interface
	 *
	 * @param self Interface instance
	 * @param session Session to get the message for
	 * @param buf Buffer the message is written to
	 * @param size Size of @p buf, at least desc->mtu
	 * @param len Length of the returned message
	 * @param timeout_ms Time to wait for a message, 0 to return immediately
	 *
	 * @return REMOTE_INTERFACE_RET_OK if a message was returned,
	 *         REMOTE_INTERFACE_RET_TIMEOUT if no message was available within @p timeout_ms,
	 *         REMOTE_INTERFACE_RET_CLOSED if the session was closed by the interface,
	 *         REMOTE_INTERFACE_RET_BAD_ARG on an unknown session or if @p size is smaller than desc->mtu.
	 */
	remote_interface_ret_t (*read)(RemoteInterface *self, void *session, void *buf, size_t size, size_t *len,
	                               uint32_t timeout_ms);
};

struct remote_interface {
	const struct remote_interface_vmt *vmt;
	void *parent;

	/** Description of the interface, owned by the implementation and static for its whole lifetime. */
	const struct remote_interface_desc *desc;
};
