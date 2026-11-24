/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic TCP connection interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * A Tcp interface provides reliable bidirectional byte stream connections to remote hosts. It is implemented by
 * a local TCP/IP stack or by a driver of a network module with an internal stack (AT-command Wi-Fi/cellular
 * modules), the user does not need to know which one. Host names are resolved by the implementation.
 *
 * Each connection is a session. The implementation owns desc->max_sessions preallocated session slots, connect()
 * returns an opaque session pointer and close() frees it. All methods block up to their timeout. Implementations
 * are responsible for locking, different sessions may be used from different tasks concurrently, a single session
 * must not be used from more than one task at a time.
 *
 * The current scope covers the client role. Methods for the server role (listen/accept) are reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <interfaces/notify.h>


typedef enum {
	/** The function performed as intended with no unexpected result. */
	TCP_RET_OK = 0,

	/** A generic failure not covered by a more specific code. */
	TCP_RET_FAILED,

	/** A NULL pointer was given as an instance or a required argument. */
	TCP_RET_NULL,

	/** A wrong argument value was used (an unknown session, a host name too long for the implementation). */
	TCP_RET_BAD_ARG,

	/** The network is not available (eg. the Wi-Fi link is down). */
	TCP_RET_BAD_STATE,

	/** The operation did not complete within the given timeout. */
	TCP_RET_TIMEOUT,

	/** No free session slot is available. */
	TCP_RET_NOMEM,

	/** The host could not be resolved, did not respond or refused the connection. */
	TCP_RET_CONNECT_FAILED,

	/** The connection was closed by the remote host or lost, no more data can be sent. */
	TCP_RET_CLOSED,

	/** The connection was closed by the remote host and all received data was already read. */
	TCP_RET_EOF,
} tcp_ret_t;


/**
 * Description of the implementation, owned by it and static for its whole lifetime.
 */
struct tcp_desc {
	/** Maximum number of concurrently open connections. */
	uint8_t max_sessions;
};


typedef struct tcp Tcp;

struct tcp_vmt {
	/**
	 * @brief Connect to a remote host
	 *
	 * Allocate a session slot, resolve @p host and establish the connection.
	 *
	 * @param self Tcp interface instance
	 * @param host Host name or a textual IP address of the remote host
	 * @param port Remote port
	 * @param notify Notify interface called when new data is received or the connection is closed, the value is
	 *               the number of bytes waiting to be read (0 when closed). NULL if the user polls recv().
	 * @param session The opaque session pointer is returned here
	 * @param timeout_ms Time to wait for the connection to be established
	 *
	 * @return TCP_RET_OK if the connection was established,
	 *         TCP_RET_NULL if @p host or @p session is NULL,
	 *         TCP_RET_BAD_ARG if @p host is not usable by the implementation,
	 *         TCP_RET_BAD_STATE if the network is not available,
	 *         TCP_RET_NOMEM if no session slot is free,
	 *         TCP_RET_CONNECT_FAILED if the host could not be resolved or connected,
	 *         TCP_RET_TIMEOUT if the connection was not established within @p timeout_ms,
	 *         TCP_RET_FAILED on other error.
	 */
	tcp_ret_t (*connect)(Tcp *self, const char *host, uint16_t port, Notify *notify, void **session,
	                     uint32_t timeout_ms);

	/**
	 * @brief Close a connection
	 *
	 * Close the connection if it is still open, drop all unread data and free the session slot. Must be called
	 * exactly once for every connected session, including sessions closed by the remote host. The session
	 * pointer is invalid afterwards.
	 *
	 * @param self Tcp interface instance
	 * @param session Session to close
	 *
	 * @return TCP_RET_OK on success,
	 *         TCP_RET_NULL or TCP_RET_BAD_ARG on an invalid session.
	 */
	tcp_ret_t (*close)(Tcp *self, void *session);

	/**
	 * @brief Send data
	 *
	 * Send all @p len bytes, the implementation splits them as needed. The buffer is owned by the caller and may
	 * be reused as soon as the method returns. Returning TCP_RET_OK means the data was accepted by the stack,
	 * not that it was received by the remote host.
	 *
	 * @param self Tcp interface instance
	 * @param session Session of the connection
	 * @param buf Data to send
	 * @param len Length of the data
	 * @param timeout_ms Time to wait for all the data to be accepted
	 *
	 * @return TCP_RET_OK if all data was accepted,
	 *         TCP_RET_BAD_ARG on an unknown session,
	 *         TCP_RET_CLOSED if the connection is closed,
	 *         TCP_RET_TIMEOUT if the data was not accepted within @p timeout_ms, part of it may have been sent,
	 *         TCP_RET_FAILED on other error.
	 */
	tcp_ret_t (*send)(Tcp *self, void *session, const void *buf, size_t len, uint32_t timeout_ms);

	/**
	 * @brief Receive data
	 *
	 * Return any number of received bytes up to @p size.
	 *
	 * @param self Tcp interface instance
	 * @param session Session of the connection
	 * @param buf Buffer the data is written to
	 * @param size Size of @p buf
	 * @param len Number of bytes actually received
	 * @param timeout_ms Time to wait for at least one byte, 0 to return immediately
	 *
	 * @return TCP_RET_OK if at least one byte was received,
	 *         TCP_RET_EOF if the connection is closed and all data was read, @p len is 0,
	 *         TCP_RET_BAD_ARG on an unknown session,
	 *         TCP_RET_TIMEOUT if no data was available within @p timeout_ms,
	 *         TCP_RET_FAILED on other error.
	 */
	tcp_ret_t (*recv)(Tcp *self, void *session, void *buf, size_t size, size_t *len, uint32_t timeout_ms);

	/* reserved: listen/accept for the server role */
};

struct tcp {
	const struct tcp_vmt *vmt;
	void *parent;

	/** Description of the implementation, owned by it and static for its whole lifetime. */
	const struct tcp_desc *desc;
};
