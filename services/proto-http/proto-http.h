/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * HTTP/1.1 client over a Tcp or Tls connection
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * ProtoHttp implements the client side of the Http interface with absolute targets (http://host[:port]/path,
 * https://...). Each session opens its own connection through the configured Tcp (http) or Tls (https) interface
 * and talks HTTP/1.1 over the returned Stream. Every request is sent with "Connection: close", the connection is
 * closed with the session.
 *
 * Each session owns a single buffer of CONFIG_SERVICE_PROTO_HTTP_BUF_SIZE bytes. It holds the request line and the
 * request headers until the first write() or get_response(), the request body is buffered in it to coalesce small
 * writes. After get_response() it holds the response headers not consumed by the framing, which are returned by
 * read_header(). The whole request head must fit the buffer, add_header() returns HTTP_RET_NOMEM otherwise.
 * A response header longer than the buffer is skipped.
 *
 * A request body of an unknown length is sent with the chunked transfer coding. Chunked responses are decoded.
 *
 * If credentials are configured, a request without a body answered with "401 Unauthorized" and a Basic challenge
 * is repeated once on a new connection with the Basic credentials. The request head is kept in the buffer until
 * the response arrives for that, leaving less space for the response headers. A request with a body is not
 * repeated, its 401 response is returned. The credentials are sent only when the server asks for them.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include <main.h>
#include <interfaces/http.h>
#include <interfaces/tcp.h>
#include <interfaces/tls.h>
#include <interfaces/stream.h>


typedef enum {
	PROTO_HTTP_RET_OK = 0,
	PROTO_HTTP_RET_FAILED,
	PROTO_HTTP_RET_NULL,
} proto_http_ret_t;


struct proto_http_conf {
	/** Connections for http:// targets, NULL if http is not supported. */
	Tcp *tcp;

	/** Connections for https:// targets, NULL if https is not supported. */
	Tls *tls;

	/** TLS parameters of the https connections, server_name is usually NULL to use the target host. The strings
	 *  are borrowed and must outlive the instance. */
	struct tls_params tls_params;

	/** Credentials for the HTTP Basic authentication, NULL username if none. The strings are borrowed and must
	 *  outlive the instance. */
	const char *username;
	const char *password;
};


enum proto_http_state {
	/* open() done, the request head is being assembled. */
	PROTO_HTTP_STATE_HEAD = 0,
	/* The request head is complete, the body is being written. */
	PROTO_HTTP_STATE_BODY,
	/* The response is available, its headers can be read. */
	PROTO_HTTP_STATE_RESPONSE_HEAD,
	/* The response body is being read. */
	PROTO_HTTP_STATE_RESPONSE_BODY,
	/* The exchange failed, only close() is allowed. */
	PROTO_HTTP_STATE_FAILED,
};

enum proto_http_framing {
	/* The body length is known, body_left bytes remain. */
	PROTO_HTTP_FRAMING_LENGTH = 0,
	/* Chunked transfer coding. */
	PROTO_HTTP_FRAMING_CHUNKED,
	/* The body ends when the connection is closed. */
	PROTO_HTTP_FRAMING_CLOSE,
};

struct proto_http_session {
	bool used;
	enum proto_http_state state;

	/* The connection was made through Tls, it is closed through it. The port is kept for a reconnection. */
	bool tls;
	uint16_t port;
	Stream *stream;

	/* Request body framing, req_left bytes remain to be written if the length is known. */
	enum proto_http_framing req_framing;
	size_t req_left;

	/* The request has no response body (HEAD). */
	bool no_body;

	/* Request body data was written, the request cannot be repeated. */
	bool body_sent;

	/* The response asks for the Basic authentication (WWW-Authenticate). */
	bool basic_auth;

	/* Response body framing. body_left is the remaining body length or the remaining length of the current
	 * chunk. In a chunked body, chunk_crlf marks the CRLF following the chunk data still to be read and
	 * trailers the trailer section following the last chunk. done marks the whole body read. */
	enum proto_http_framing resp_framing;
	size_t body_left;
	bool chunk_crlf;
	bool trailers;
	bool done;

	/* Buffer layout: the NUL terminated host name is kept at the end in [limit, sizeof(buf)) for a reconnection.
	 * Before get_response() the request data to be sent is in [0, tx_len). After it, the request head which may
	 * be repeated stays in [0, base) (base is 0 if it is not), the kept response headers are NUL terminated lines
	 * in [hdr_pos, hdr_end) and the received data not processed yet is in [rx_pos, rx_end), rx_pos >= hdr_end. */
	size_t limit;
	size_t base;
	size_t tx_len;
	size_t hdr_pos;
	size_t hdr_end;
	size_t rx_pos;
	size_t rx_end;
	char buf[CONFIG_SERVICE_PROTO_HTTP_BUF_SIZE];
};

typedef struct {
	struct proto_http_conf conf;

	struct proto_http_session sessions[CONFIG_SERVICE_PROTO_HTTP_MAX_SESSIONS];

	/* Http client interface. */
	Http http;
} ProtoHttp;


proto_http_ret_t proto_http_init(ProtoHttp *self, const struct proto_http_conf *conf);
proto_http_ret_t proto_http_free(ProtoHttp *self);
proto_http_ret_t proto_http_start(ProtoHttp *self);
proto_http_ret_t proto_http_stop(ProtoHttp *self);
