/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic HTTP request/response exchange interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * An Http interface takes an HTTP request and produces an HTTP response. The same interface is used in both
 * directions:
 *
 * - A service implementing HTTP routes (a REST API, a file server) implements it, an HTTP server service parses
 *   requests received over its TCP connections and passes them to the route services.
 * - A driver of a network module with an internal network stack (AT-command Wi-Fi/cellular modules) implements it,
 *   applications perform HTTP requests through the module directly without any local TCP/IP stack.
 *
 * Each request is processed in a session. The conceptual documentation, including the session state machine, is
 * in doc/interfaces/http.md.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


typedef enum {
	/** The function performed as intended with no unexpected result. */
	HTTP_RET_OK = 0,

	/** A generic failure not covered by a more specific code. */
	HTTP_RET_FAILED,

	/** A NULL pointer was given as an instance or a required argument. */
	HTTP_RET_NULL,

	/** A wrong argument value was used (an unknown session, a malformed target, a write past the announced
	 *  content length). */
	HTTP_RET_BAD_ARG,

	/** The method was called in a wrong session state (eg. add_header() after write(), read() before
	 *  get_response()). */
	HTTP_RET_BAD_STATE,

	/** The operation did not complete within the given timeout. */
	HTTP_RET_TIMEOUT,

	/** A preallocated resource of the implementation (header storage) is exhausted. */
	HTTP_RET_NOMEM,

	/** The implementation does not support the requested method, URL scheme (eg. https) or framing. */
	HTTP_RET_NOT_IMPLEMENTED,

	/** The target is not served by this implementation. A server returns 404 to the client or tries the next
	 *  route service. */
	HTTP_RET_NOT_FOUND,

	/** The remote host could not be resolved or connected, or the connection was lost before the response was
	 *  received. */
	HTTP_RET_CONNECT_FAILED,

	/** No more headers or body data to read. */
	HTTP_RET_EOF,
} http_ret_t;


enum http_method {
	HTTP_METHOD_GET = 0,
	HTTP_METHOD_HEAD,
	HTTP_METHOD_POST,
	HTTP_METHOD_PUT,
	HTTP_METHOD_DELETE,
	HTTP_METHOD_PATCH,
	HTTP_METHOD_OPTIONS,
};

/** Method name lookup table initializer, indexed by enum http_method. */
#define HTTP_METHOD_NAMES { "GET", "HEAD", "POST", "PUT", "DELETE", "PATCH", "OPTIONS" }


/**
 * Request line and request body framing. Valid only during the open() call, the implementation copies what it
 * needs.
 */
struct http_request {
	enum http_method method;

	/**
	 * Request target. An absolute URL ("http://example.com:8080/api/v1?x=1") when calling a client (a network
	 * module driver), the origin-form path including the query ("/api/v1?x=1") when a server calls a route
	 * service.
	 */
	const char *target;

	/** The request body length is known in advance and @p content_length is valid. */
	bool has_content_length;

	/** Length of the request body in bytes, 0 if there is no body. */
	size_t content_length;
};

/**
 * Response status line and response body framing.
 */
struct http_response {
	/** HTTP status code (200, 404, ...). */
	uint16_t status;

	/** The response body length is known in advance and @p content_length is valid. If not, the body is read
	 *  until read() returns HTTP_RET_EOF. */
	bool has_content_length;

	/** Length of the response body in bytes, 0 if there is no body. */
	size_t content_length;
};


/**
 * Description of the implementation, owned by it and static for its whole lifetime.
 */
struct http_desc {
	/** Maximum number of concurrently open sessions. */
	uint8_t max_sessions;
};


typedef struct http Http;

struct http_vmt {
	/**
	 * @brief Open a session and start a new request
	 *
	 * Allocate a session slot and start processing the request line. A client implementation may resolve and
	 * connect to the host here or postpone it to get_response(). A route service checks whether it serves the
	 * target.
	 *
	 * @param self Http interface instance
	 * @param request Request line and body framing, valid only during the call
	 * @param session The opaque session pointer is returned here
	 * @param timeout_ms Time to wait for a free session slot (and for the connection, if made here), 0 to not
	 *                   wait at all
	 *
	 * @return HTTP_RET_OK on success,
	 *         HTTP_RET_NULL if @p request, its target or @p session is NULL,
	 *         HTTP_RET_BAD_ARG on a malformed target,
	 *         HTTP_RET_NOT_FOUND if a route service does not serve the target,
	 *         HTTP_RET_NOT_IMPLEMENTED if the method or the URL scheme is not supported, or if the implementation
	 *         needs the request body length in advance and it is not known,
	 *         HTTP_RET_CONNECT_FAILED if the connection was made here and failed,
	 *         HTTP_RET_TIMEOUT if no slot was free within @p timeout_ms,
	 *         HTTP_RET_FAILED on other error.
	 */
	http_ret_t (*open)(Http *self, const struct http_request *request, void **session, uint32_t timeout_ms);

	/**
	 * @brief Close a session
	 *
	 * Abort the request if it is unfinished (close the connection, drop the unread response) and free the session
	 * slot. Must be called exactly once for every opened session, including sessions where another method failed.
	 * The session pointer is invalid afterwards.
	 *
	 * @param self Http interface instance
	 * @param session Session to close
	 *
	 * @return HTTP_RET_OK on success,
	 *         HTTP_RET_NULL or HTTP_RET_BAD_ARG on an invalid session.
	 */
	http_ret_t (*close)(Http *self, void *session);

	/**
	 * @brief Add a request header
	 *
	 * Allowed only before the first write(). Headers are passed in the order they were received or should be
	 * sent, the same name may be added multiple times. Framing headers (Content-Length, Transfer-Encoding) and
	 * connection management headers (Host, Connection) are never added, they are handled by the server or the
	 * client implementation. The implementation may ignore headers it does not need.
	 *
	 * @param self Http interface instance
	 * @param session Session of the request
	 * @param name Header name, case insensitive
	 * @param value Header value without leading and trailing whitespace
	 *
	 * @return HTTP_RET_OK if the header was accepted or ignored,
	 *         HTTP_RET_BAD_STATE if called after write() or get_response(),
	 *         HTTP_RET_NOMEM if the implementation has no space left for the header,
	 *         HTTP_RET_BAD_ARG on an unknown session or a malformed header.
	 */
	http_ret_t (*add_header)(Http *self, void *session, const char *name, const char *value);

	/**
	 * @brief Write a chunk of the request body
	 *
	 * May be called multiple times, the whole chunk is either accepted or not. If the body length was announced
	 * in open(), exactly that many bytes must be written in total before get_response(). The buffer is owned by
	 * the caller and may be reused as soon as the method returns.
	 *
	 * @param self Http interface instance
	 * @param session Session of the request
	 * @param buf Request body chunk
	 * @param len Length of the chunk
	 * @param timeout_ms Time to wait for the chunk to be accepted, 0 to not wait at all
	 *
	 * @return HTTP_RET_OK if the chunk was accepted,
	 *         HTTP_RET_BAD_STATE if called after get_response(),
	 *         HTTP_RET_BAD_ARG on an unknown session or a write past the announced content length,
	 *         HTTP_RET_CONNECT_FAILED if the connection was lost,
	 *         HTTP_RET_TIMEOUT if the chunk was not accepted within @p timeout_ms,
	 *         HTTP_RET_FAILED on other error (a route service rejected the body).
	 */
	http_ret_t (*write)(Http *self, void *session, const void *buf, size_t len, uint32_t timeout_ms);

	/**
	 * @brief Finish the request and wait for the response
	 *
	 * Mark the end of the request body and wait until the response status line and headers are available.
	 * A route service usually processes the request here. Errors processed by a route service (bad request
	 * content, missing resources) are reported as a regular response with the corresponding status code.
	 *
	 * @param self Http interface instance
	 * @param session Session of the request
	 * @param response Response status and body framing
	 * @param timeout_ms Time to wait for the response, 0 to not wait at all
	 *
	 * @return HTTP_RET_OK if a response is available,
	 *         HTTP_RET_BAD_STATE if called twice or if fewer bytes than the announced content length were written,
	 *         HTTP_RET_BAD_ARG on an unknown session,
	 *         HTTP_RET_CONNECT_FAILED if the host could not be connected or the connection was lost,
	 *         HTTP_RET_TIMEOUT if no response was received within @p timeout_ms,
	 *         HTTP_RET_FAILED on other error.
	 */
	http_ret_t (*get_response)(Http *self, void *session, struct http_response *response, uint32_t timeout_ms);

	/**
	 * @brief Read the next response header
	 *
	 * Headers are returned one by one in order, only after get_response() and before the first read(). Framing
	 * and connection management headers are not returned, framing is described by struct http_response.
	 * An implementation may return only a subset of the headers (eg. a module reporting only Content-Type).
	 * A header which does not fit the buffers is skipped.
	 *
	 * @param self Http interface instance
	 * @param session Session of the request
	 * @param name Buffer for the NUL terminated header name
	 * @param name_size Size of @p name
	 * @param value Buffer for the NUL terminated header value
	 * @param value_size Size of @p value
	 *
	 * @return HTTP_RET_OK if a header was returned,
	 *         HTTP_RET_EOF if there are no more headers,
	 *         HTTP_RET_BAD_STATE if called before get_response() or after read(),
	 *         HTTP_RET_BAD_ARG on an unknown session.
	 */
	http_ret_t (*read_header)(Http *self, void *session, char *name, size_t name_size, char *value,
	                          size_t value_size);

	/**
	 * @brief Read a chunk of the response body
	 *
	 * Allowed after get_response(), unread headers are skipped. Returns any number of bytes up to @p size.
	 *
	 * @param self Http interface instance
	 * @param session Session of the request
	 * @param buf Buffer the body chunk is written to
	 * @param size Size of @p buf
	 * @param len Number of bytes actually read
	 * @param timeout_ms Time to wait for at least one byte, 0 to return immediately
	 *
	 * @return HTTP_RET_OK if at least one byte was read,
	 *         HTTP_RET_EOF if the whole body was read (immediately for responses without a body), @p len is 0,
	 *         HTTP_RET_BAD_STATE if called before get_response(),
	 *         HTTP_RET_BAD_ARG on an unknown session,
	 *         HTTP_RET_CONNECT_FAILED if the connection was lost before the whole body was received,
	 *         HTTP_RET_TIMEOUT if no data was available within @p timeout_ms,
	 *         HTTP_RET_FAILED on other error.
	 */
	http_ret_t (*read)(Http *self, void *session, void *buf, size_t size, size_t *len, uint32_t timeout_ms);
};

struct http {
	const struct http_vmt *vmt;
	void *parent;

	/** Description of the implementation, owned by it and static for its whole lifetime. */
	const struct http_desc *desc;
};
