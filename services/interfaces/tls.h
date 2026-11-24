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
 * Each open connection is a Stream, its use, blocking and locking rules are the same as for Tcp. The current scope
 * covers the client role.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <interfaces/notify.h>
#include <interfaces/stream.h>


typedef enum {
	/** The function performed as intended with no unexpected result. */
	TLS_RET_OK = 0,

	/** A generic failure not covered by a more specific code. */
	TLS_RET_FAILED,

	/** A NULL pointer was given as an instance or a required argument. */
	TLS_RET_NULL,

	/** A wrong argument value was used (an unknown connection, a host name or a credential name not usable by
	 *  the implementation). */
	TLS_RET_BAD_ARG,

	/** The network is not available (eg. the Wi-Fi link is down). */
	TLS_RET_BAD_STATE,

	/** The operation did not complete within the given timeout. */
	TLS_RET_TIMEOUT,

	/** No free connection slot is available. */
	TLS_RET_NOMEM,

	/** The host could not be resolved or connected, or the TLS handshake failed (including a failed certificate
	 *  verification). */
	TLS_RET_CONNECT_FAILED,
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
	 * Allocate a connection slot, resolve @p host, establish the connection and perform the TLS handshake.
	 *
	 * @param self Tls interface instance
	 * @param host Host name or a textual IP address of the remote host
	 * @param port Remote port
	 * @param params TLS parameters, valid only during the call
	 * @param notify Notify interface called when new data is received or the connection is closed, the value is
	 *               the number of bytes waiting to be read (0 when closed). NULL if the user blocks in read().
	 * @param stream The Stream of the connection is returned here, owned by the implementation
	 * @param timeout_ms Time to wait for the connection to be established
	 *
	 * @return TLS_RET_OK if the connection was established,
	 *         TLS_RET_NULL if @p host, @p params or @p stream is NULL,
	 *         TLS_RET_BAD_ARG if @p host or a credential name is not usable by the implementation,
	 *         TLS_RET_BAD_STATE if the network is not available,
	 *         TLS_RET_NOMEM if no connection slot is free,
	 *         TLS_RET_CONNECT_FAILED if the host could not be connected or the handshake failed,
	 *         TLS_RET_TIMEOUT if the connection was not established within @p timeout_ms,
	 *         TLS_RET_FAILED on other error.
	 */
	tls_ret_t (*connect)(Tls *self, const char *host, uint16_t port, const struct tls_params *params,
	                     Notify *notify, Stream **stream, uint32_t timeout_ms);

	/**
	 * @brief Close a connection
	 *
	 * Same as Tcp close().
	 *
	 * @param self Tls interface instance
	 * @param stream Stream of the connection to close
	 *
	 * @return TLS_RET_OK on success,
	 *         TLS_RET_NULL or TLS_RET_BAD_ARG on an invalid Stream.
	 */
	tls_ret_t (*close)(Tls *self, Stream *stream);
};

struct tls {
	const struct tls_vmt *vmt;
	void *parent;

	/** Description of the implementation, owned by it and static for its whole lifetime. */
	const struct tls_desc *desc;
};
