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
 * Each open connection is a Stream. The implementation owns desc->max_sessions preallocated connection slots,
 * connect() returns the Stream of the connection and close() frees its slot. The Stream is used by any service
 * talking Stream (a protocol implementation) without knowing it runs over TCP:
 *
 * - write() blocks until all data was accepted by the stack, write_timeout() up to its timeout. Accepted data is
 *   not necessarily received by the remote host yet.
 * - read() blocks until at least one byte was received, read_timeout() up to its timeout.
 * - Both return STREAM_RET_EOF once the connection was closed by the remote host or lost, read() only after all
 *   received data was read. The Stream stays valid until close() is called.
 *
 * Implementations are responsible for locking, different connections may be used from different tasks
 * concurrently, a single connection must not be used from more than one task at a time.
 *
 * The current scope covers the client role. Methods for the server role (listen/accept) are reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <interfaces/notify.h>
#include <interfaces/stream.h>


typedef enum {
	/** The function performed as intended with no unexpected result. */
	TCP_RET_OK = 0,

	/** A generic failure not covered by a more specific code. */
	TCP_RET_FAILED,

	/** A NULL pointer was given as an instance or a required argument. */
	TCP_RET_NULL,

	/** A wrong argument value was used (an unknown connection, a host name too long for the implementation). */
	TCP_RET_BAD_ARG,

	/** The network is not available (eg. the Wi-Fi link is down). */
	TCP_RET_BAD_STATE,

	/** The operation did not complete within the given timeout. */
	TCP_RET_TIMEOUT,

	/** No free connection slot is available. */
	TCP_RET_NOMEM,

	/** The host could not be resolved, did not respond or refused the connection. */
	TCP_RET_CONNECT_FAILED,
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
	 * Allocate a connection slot, resolve @p host and establish the connection.
	 *
	 * @param self Tcp interface instance
	 * @param host Host name or a textual IP address of the remote host
	 * @param port Remote port
	 * @param notify Notify interface called when new data is received or the connection is closed, the value is
	 *               the number of bytes waiting to be read (0 when closed). NULL if the user blocks in read().
	 * @param stream The Stream of the connection is returned here, owned by the implementation
	 * @param timeout_ms Time to wait for the connection to be established
	 *
	 * @return TCP_RET_OK if the connection was established,
	 *         TCP_RET_NULL if @p host or @p stream is NULL,
	 *         TCP_RET_BAD_ARG if @p host is not usable by the implementation,
	 *         TCP_RET_BAD_STATE if the network is not available,
	 *         TCP_RET_NOMEM if no connection slot is free,
	 *         TCP_RET_CONNECT_FAILED if the host could not be resolved or connected,
	 *         TCP_RET_TIMEOUT if the connection was not established within @p timeout_ms,
	 *         TCP_RET_FAILED on other error.
	 */
	tcp_ret_t (*connect)(Tcp *self, const char *host, uint16_t port, Notify *notify, Stream **stream,
	                     uint32_t timeout_ms);

	/**
	 * @brief Close a connection
	 *
	 * Close the connection if it is still open, drop all unread data and free the connection slot. Must be
	 * called exactly once for every connected Stream, including connections closed by the remote host. The
	 * Stream is invalid afterwards.
	 *
	 * @param self Tcp interface instance
	 * @param stream Stream of the connection to close
	 *
	 * @return TCP_RET_OK on success,
	 *         TCP_RET_NULL or TCP_RET_BAD_ARG on an invalid Stream.
	 */
	tcp_ret_t (*close)(Tcp *self, Stream *stream);

	/* reserved: listen/accept for the server role */
};

struct tcp {
	const struct tcp_vmt *vmt;
	void *parent;

	/** Description of the implementation, owned by it and static for its whole lifetime. */
	const struct tcp_desc *desc;
};
