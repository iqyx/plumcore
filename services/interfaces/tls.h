/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic TLS connection interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * A Tls interface provides TLS secured byte stream connections to remote hosts. It is used exactly like Tcp,
 * only connect() takes the TLS parameters in addition. It is implemented by a driver of a network module with
 * an internal TLS stack, or by a software TLS service running on top of a Tcp interface.
 *
 * Certificates and keys are referenced by implementation defined names. A network module driver uses the names
 * of the files stored in the module (provisioned beforehand), a software implementation may use names of
 * credentials in its own store.
 *
 * Sessions, blocking and locking rules are the same as for Tcp. The current scope covers the client role.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <interfaces/notify.h>


typedef enum {
	/** The function performed as intended with no unexpected result. */
	TLS_RET_OK = 0,

	/** A generic failure not covered by a more specific code. */
	TLS_RET_FAILED,

	/** A NULL pointer was given as an instance or a required argument. */
	TLS_RET_NULL,

	/** A wrong argument value was used (an unknown session, a host name or a credential name not usable by the
	 *  implementation). */
	TLS_RET_BAD_ARG,

	/** The network is not available (eg. the Wi-Fi link is down). */
	TLS_RET_BAD_STATE,

	/** The operation did not complete within the given timeout. */
	TLS_RET_TIMEOUT,

	/** No free session slot is available. */
	TLS_RET_NOMEM,

	/** The host could not be resolved or connected, or the TLS handshake failed (including a failed certificate
	 *  verification). */
	TLS_RET_CONNECT_FAILED,

	/** The connection was closed by the remote host or lost, no more data can be sent. */
	TLS_RET_CLOSED,

	/** The connection was closed by the remote host and all received data was already read. */
	TLS_RET_EOF,
} tls_ret_t;


/**
 * TLS parameters of a connection. Valid only during the connect() call.
 */
struct tls_params {
	/** Server name sent in the SNI extension and checked against the server certificate. NULL to use the host
	 *  name given to connect(). */
	const char *server_name;

	/** CA certificate used to verify the server certificate. NULL to skip the server verification (insecure,
	 *  for testing only). */
	const char *ca;

	/** Client certificate and its private key for client authentication. NULL if the client does not
	 *  authenticate. Both or none must be set. */
	const char *cert;
	const char *key;
};


/**
 * Description of the implementation, owned by it and static for its whole lifetime.
 */
struct tls_desc {
	/** Maximum number of concurrently open connections. */
	uint8_t max_sessions;
};


typedef struct tls Tls;

struct tls_vmt {
	/**
	 * @brief Connect to a remote host
	 *
	 * Allocate a session slot, resolve @p host, establish the connection and perform the TLS handshake.
	 *
	 * @param self Tls interface instance
	 * @param host Host name or a textual IP address of the remote host
	 * @param port Remote port
	 * @param params TLS parameters, valid only during the call
	 * @param notify Notify interface called when new data is received or the connection is closed, the value is
	 *               the number of bytes waiting to be read (0 when closed). NULL if the user polls recv().
	 * @param session The opaque session pointer is returned here
	 * @param timeout_ms Time to wait for the connection to be established
	 *
	 * @return TLS_RET_OK if the connection was established,
	 *         TLS_RET_NULL if @p host, @p params or @p session is NULL,
	 *         TLS_RET_BAD_ARG if @p host or a credential name is not usable by the implementation,
	 *         TLS_RET_BAD_STATE if the network is not available,
	 *         TLS_RET_NOMEM if no session slot is free,
	 *         TLS_RET_CONNECT_FAILED if the host could not be connected or the handshake failed,
	 *         TLS_RET_TIMEOUT if the connection was not established within @p timeout_ms,
	 *         TLS_RET_FAILED on other error.
	 */
	tls_ret_t (*connect)(Tls *self, const char *host, uint16_t port, const struct tls_params *params,
	                     Notify *notify, void **session, uint32_t timeout_ms);

	/**
	 * @brief Close a connection
	 *
	 * Same as Tcp close().
	 *
	 * @param self Tls interface instance
	 * @param session Session to close
	 *
	 * @return TLS_RET_OK on success,
	 *         TLS_RET_NULL or TLS_RET_BAD_ARG on an invalid session.
	 */
	tls_ret_t (*close)(Tls *self, void *session);

	/**
	 * @brief Send data
	 *
	 * Same as Tcp send().
	 *
	 * @param self Tls interface instance
	 * @param session Session of the connection
	 * @param buf Data to send
	 * @param len Length of the data
	 * @param timeout_ms Time to wait for all the data to be accepted
	 *
	 * @return TLS_RET_OK if all data was accepted,
	 *         TLS_RET_BAD_ARG on an unknown session,
	 *         TLS_RET_CLOSED if the connection is closed,
	 *         TLS_RET_TIMEOUT if the data was not accepted within @p timeout_ms, part of it may have been sent,
	 *         TLS_RET_FAILED on other error.
	 */
	tls_ret_t (*send)(Tls *self, void *session, const void *buf, size_t len, uint32_t timeout_ms);

	/**
	 * @brief Receive data
	 *
	 * Same as Tcp recv().
	 *
	 * @param self Tls interface instance
	 * @param session Session of the connection
	 * @param buf Buffer the data is written to
	 * @param size Size of @p buf
	 * @param len Number of bytes actually received
	 * @param timeout_ms Time to wait for at least one byte, 0 to return immediately
	 *
	 * @return TLS_RET_OK if at least one byte was received,
	 *         TLS_RET_EOF if the connection is closed and all data was read, @p len is 0,
	 *         TLS_RET_BAD_ARG on an unknown session,
	 *         TLS_RET_TIMEOUT if no data was available within @p timeout_ms,
	 *         TLS_RET_FAILED on other error.
	 */
	tls_ret_t (*recv)(Tls *self, void *session, void *buf, size_t size, size_t *len, uint32_t timeout_ms);
};

struct tls {
	const struct tls_vmt *vmt;
	void *parent;

	/** Description of the implementation, owned by it and static for its whole lifetime. */
	const struct tls_desc *desc;
};
