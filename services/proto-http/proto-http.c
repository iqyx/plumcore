/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * HTTP/1.1 client over a Tcp or Tls connection
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <main.h>
#include <interfaces/http.h>
#include <interfaces/tcp.h>
#include <interfaces/tls.h>
#include <interfaces/stream.h>

#include <base64.h>

#include "proto-http.h"

#define MODULE_NAME "proto-http"


static const char * const proto_http_method_names[] = HTTP_METHOD_NAMES;

static const struct http_desc proto_http_desc = {
	.max_sessions = CONFIG_SERVICE_PROTO_HTTP_MAX_SESSIONS,
};


/* Remaining time of an operation started at @p start with the timeout @p timeout_ms. */
static uint32_t proto_http_left(TickType_t start, uint32_t timeout_ms) {
	uint32_t elapsed = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount() - start);
	return (elapsed < timeout_ms) ? timeout_ms - elapsed : 0;
}


static http_ret_t proto_http_stream_ret(stream_ret_t ret) {
	switch (ret) {
		case STREAM_RET_OK:
			return HTTP_RET_OK;
		case STREAM_RET_TIMEOUT:
			return HTTP_RET_TIMEOUT;
		case STREAM_RET_EOF:
			return HTTP_RET_CONNECT_FAILED;
		default:
			return HTTP_RET_FAILED;
	}
}


/* Test whether the header name @p name of length @p len is @p expected, case insensitive. */
static bool proto_http_name_is(const char *name, size_t len, const char *expected) {
	return strlen(expected) == len && !strncasecmp(name, expected, len);
}


/* Find the used session of an opaque session pointer, NULL if there is none. */
static struct proto_http_session *proto_http_session_get(ProtoHttp *self, void *session) {
	for (size_t i = 0; i < CONFIG_SERVICE_PROTO_HTTP_MAX_SESSIONS; i++) {
		if (&self->sessions[i] == session) {
			return self->sessions[i].used ? &self->sessions[i] : NULL;
		}
	}
	return NULL;
}


/**********************************************************************************************************************
 * Sending the request
 **********************************************************************************************************************/

/* Append @p len bytes to the request data buffered for sending, keeping @p reserve bytes free. Returns false if they
 * do not fit. */
static bool proto_http_append(struct proto_http_session *s, const void *data, size_t len, size_t reserve) {
	if (len + reserve > s->limit - s->tx_len) {
		return false;
	}
	memcpy(s->buf + s->tx_len, data, len);
	s->tx_len += len;
	return true;
}


/* Append a string to the request head, keeping space for the CRLF terminating it. */
static bool proto_http_append_head(struct proto_http_session *s, const char *str) {
	return proto_http_append(s, str, strlen(str), 2);
}


/* Terminate the request head, the body follows. */
static void proto_http_end_head(struct proto_http_session *s) {
	if (s->state == PROTO_HTTP_STATE_HEAD) {
		proto_http_append(s, "\r\n", 2, 0);
		s->state = PROTO_HTTP_STATE_BODY;
	}
}


/* Send the request data buffered so far. */
static http_ret_t proto_http_flush(struct proto_http_session *s, uint32_t timeout_ms) {
	if (s->tx_len == 0) {
		return HTTP_RET_OK;
	}
	size_t written = 0;
	stream_ret_t ret = s->stream->vmt->write_timeout(s->stream, s->buf, s->tx_len, &written, timeout_ms);
	s->tx_len = 0;
	return proto_http_stream_ret(ret);
}


/* Send request data, small pieces are coalesced in the buffer. */
static http_ret_t proto_http_send(struct proto_http_session *s, const void *data, size_t len, TickType_t start,
                                  uint32_t timeout_ms) {
	if (proto_http_append(s, data, len, 0)) {
		return HTTP_RET_OK;
	}
	http_ret_t ret = proto_http_flush(s, proto_http_left(start, timeout_ms));
	if (ret != HTTP_RET_OK) {
		return ret;
	}
	if (proto_http_append(s, data, len, 0)) {
		return HTTP_RET_OK;
	}
	size_t written = 0;
	return proto_http_stream_ret(s->stream->vmt->write_timeout(s->stream, data, len, &written,
	                                                           proto_http_left(start, timeout_ms)));
}


/**********************************************************************************************************************
 * Receiving the response
 **********************************************************************************************************************/

/* Get the next received line, NUL terminated without its line ending. The unprocessed received data is first moved
 * right behind the kept headers, the line starts at hdr_end. A line which does not fit the buffer is skipped if
 * @p skip_long is set, otherwise it fails. */
static http_ret_t proto_http_line(struct proto_http_session *s, char **line, bool skip_long, TickType_t start,
                                  uint32_t timeout_ms) {
	bool skipping = false;
	while (true) {
		memmove(s->buf + s->hdr_end, s->buf + s->rx_pos, s->rx_end - s->rx_pos);
		s->rx_end -= s->rx_pos - s->hdr_end;
		s->rx_pos = s->hdr_end;

		char *nl = memchr(s->buf + s->rx_pos, '\n', s->rx_end - s->rx_pos);
		if (nl != NULL) {
			*nl = '\0';
			if (nl > s->buf + s->rx_pos && nl[-1] == '\r') {
				nl[-1] = '\0';
			}
			char *found = s->buf + s->rx_pos;
			s->rx_pos = (size_t)(nl - s->buf) + 1;
			if (skipping) {
				skipping = false;
				continue;
			}
			*line = found;
			return HTTP_RET_OK;
		}

		if (s->rx_end == s->limit) {
			if (!skip_long) {
				return HTTP_RET_FAILED;
			}
			/* Drop what was received of the overlong line and skip the rest of it. */
			skipping = true;
			s->rx_end = s->rx_pos;
		}
		size_t len = 0;
		stream_ret_t ret = s->stream->vmt->read_timeout(s->stream, s->buf + s->rx_end, s->limit - s->rx_end, &len,
		                                                proto_http_left(start, timeout_ms));
		if (ret != STREAM_RET_OK) {
			return proto_http_stream_ret(ret);
		}
		s->rx_end += len;
	}
}


/* Receive the response status line and headers and set up the body framing. The framing and connection headers are
 * consumed, the others are kept for read_header() while they fit half of the space after base, the other half is
 * left for receiving the lines. Interim (1xx) responses are skipped. */
static http_ret_t proto_http_response_head(struct proto_http_session *s, struct http_response *response,
                                           TickType_t start, uint32_t timeout_ms) {
	s->rx_pos = s->base;
	s->rx_end = s->base;
	while (true) {
		s->hdr_pos = s->base;
		s->hdr_end = s->base;
		s->basic_auth = false;

		/* "HTTP/1.x <status> <reason>" */
		char *line = NULL;
		http_ret_t ret = proto_http_line(s, &line, false, start, timeout_ms);
		if (ret != HTTP_RET_OK) {
			return ret;
		}
		if (strlen(line) < 12 || strncmp(line, "HTTP/1.", 7) || line[8] != ' ') {
			return HTTP_RET_FAILED;
		}
		uint16_t status = (uint16_t)strtoul(line + 9, NULL, 10);

		bool chunked = false;
		bool has_length = false;
		size_t length = 0;
		while (true) {
			ret = proto_http_line(s, &line, true, start, timeout_ms);
			if (ret != HTTP_RET_OK) {
				return ret;
			}
			if (line[0] == '\0') {
				break;
			}
			char *colon = strchr(line, ':');
			if (colon == NULL) {
				continue;
			}
			size_t name_len = (size_t)(colon - line);
			const char *value = colon + 1;
			while (*value == ' ' || *value == '\t') {
				value++;
			}
			if (proto_http_name_is(line, name_len, "Content-Length")) {
				has_length = true;
				length = (size_t)strtoul(value, NULL, 10);
			} else if (proto_http_name_is(line, name_len, "Transfer-Encoding")) {
				chunked = (strstr(value, "chunked") != NULL);
			} else if (!proto_http_name_is(line, name_len, "Connection")) {
				if (proto_http_name_is(line, name_len, "WWW-Authenticate") && !strncasecmp(value, "Basic", 5)) {
					s->basic_auth = true;
				}
				size_t len = strlen(line) + 1;
				if (s->hdr_end + len <= s->base + (s->limit - s->base) / 2) {
					s->hdr_end += len;
				}
			}
		}
		if (status >= 100 && status < 200) {
			continue;
		}

		s->body_left = 0;
		s->chunk_crlf = false;
		s->trailers = false;
		s->done = false;
		if (s->no_body || status == 204 || status == 304) {
			s->resp_framing = PROTO_HTTP_FRAMING_LENGTH;
		} else if (chunked) {
			s->resp_framing = PROTO_HTTP_FRAMING_CHUNKED;
		} else if (has_length) {
			s->resp_framing = PROTO_HTTP_FRAMING_LENGTH;
			s->body_left = length;
		} else {
			s->resp_framing = PROTO_HTTP_FRAMING_CLOSE;
		}

		response->status = status;
		response->has_content_length = (s->resp_framing == PROTO_HTTP_FRAMING_LENGTH);
		response->content_length = s->body_left;
		return HTTP_RET_OK;
	}
}


/* Process a line delimiting the chunks of a chunked body: the CRLF following the chunk data, the size of the next
 * chunk or a trailer line. */
static http_ret_t proto_http_chunk_line(struct proto_http_session *s, TickType_t start, uint32_t timeout_ms) {
	char *line = NULL;
	http_ret_t ret = proto_http_line(s, &line, s->trailers, start, timeout_ms);
	if (ret != HTTP_RET_OK) {
		return ret;
	}
	if (s->chunk_crlf) {
		s->chunk_crlf = false;
		return (line[0] == '\0') ? HTTP_RET_OK : HTTP_RET_FAILED;
	}
	if (s->trailers) {
		if (line[0] == '\0') {
			s->done = true;
		}
		return HTTP_RET_OK;
	}

	/* "<hex size>[;extensions]" */
	char *end = NULL;
	unsigned long n = strtoul(line, &end, 16);
	if (end == line) {
		return HTTP_RET_FAILED;
	}
	if (n == 0) {
		s->trailers = true;
	} else {
		s->body_left = (size_t)n;
	}
	return HTTP_RET_OK;
}


/* Receive body data, the data already received with the headers is returned first. */
static stream_ret_t proto_http_recv(struct proto_http_session *s, void *buf, size_t size, size_t *len,
                                    uint32_t timeout_ms) {
	if (s->rx_pos < s->rx_end) {
		size_t n = s->rx_end - s->rx_pos;
		n = (n < size) ? n : size;
		memcpy(buf, s->buf + s->rx_pos, n);
		s->rx_pos += n;
		*len = n;
		return STREAM_RET_OK;
	}
	return s->stream->vmt->read_timeout(s->stream, buf, size, len, timeout_ms);
}


/**********************************************************************************************************************
 * Connection and authentication
 **********************************************************************************************************************/

/* Connect the session to the host kept at the end of its buffer. */
static http_ret_t proto_http_connect(ProtoHttp *http, struct proto_http_session *s, uint32_t timeout_ms) {
	const char *host = s->buf + s->limit;
	bool connected = false;
	bool timeout = false;
	if (s->tls) {
		tls_ret_t ret = http->conf.tls->vmt->connect(http->conf.tls, host, s->port, &http->conf.tls_params, NULL,
		                                             &s->stream, timeout_ms);
		connected = (ret == TLS_RET_OK);
		timeout = (ret == TLS_RET_TIMEOUT);
	} else {
		tcp_ret_t ret = http->conf.tcp->vmt->connect(http->conf.tcp, host, s->port, NULL, &s->stream, timeout_ms);
		connected = (ret == TCP_RET_OK);
		timeout = (ret == TCP_RET_TIMEOUT);
	}
	if (!connected) {
		s->stream = NULL;
		return timeout ? HTTP_RET_TIMEOUT : HTTP_RET_CONNECT_FAILED;
	}
	return HTTP_RET_OK;
}


static void proto_http_disconnect(ProtoHttp *http, struct proto_http_session *s) {
	if (s->stream == NULL) {
		return;
	}
	if (s->tls) {
		http->conf.tls->vmt->close(http->conf.tls, s->stream);
	} else {
		http->conf.tcp->vmt->close(http->conf.tcp, s->stream);
	}
	s->stream = NULL;
}


/* Repeat the request kept in [0, base) with the Basic credentials on a new connection. The CRLF terminating the
 * request head is replaced by the Authorization header. The credentials are joined at the end of the usable buffer
 * and encoded in front of them. If they do not fit, the 401 response is left to the caller. */
static http_ret_t proto_http_auth_retry(ProtoHttp *http, struct proto_http_session *s, struct http_response *response,
                                        TickType_t start, uint32_t timeout_ms) {
	static const char auth[] = "Authorization: Basic ";
	const char *password = (http->conf.password != NULL) ? http->conf.password : "";
	size_t user_len = strlen(http->conf.username);
	size_t cred_len = user_len + 1 + strlen(password);
	size_t b64_len = (cred_len + 2) / 3 * 4;
	size_t pos = s->base - 2;
	if (pos + sizeof(auth) - 1 + b64_len + 4 + cred_len > s->limit) {
		return HTTP_RET_OK;
	}
	char *cred = s->buf + s->limit - cred_len;
	memcpy(cred, http->conf.username, user_len);
	cred[user_len] = ':';
	memcpy(cred + user_len + 1, password, cred_len - user_len - 1);
	memcpy(s->buf + pos, auth, sizeof(auth) - 1);
	pos += sizeof(auth) - 1;
	base64_ret_t bret = base64encode(cred, cred_len, s->buf + pos, b64_len + 1);
	memset(cred, 0, cred_len);
	if (bret != BASE64_RET_OK) {
		return HTTP_RET_FAILED;
	}
	pos += b64_len;
	memcpy(s->buf + pos, "\r\n\r\n", 4);
	pos += 4;

	/* The server closes the connection after the response, the request is sent on a new one. */
	proto_http_disconnect(http, s);
	http_ret_t ret = proto_http_connect(http, s, proto_http_left(start, timeout_ms));
	if (ret != HTTP_RET_OK) {
		return ret;
	}
	s->tx_len = pos;
	ret = proto_http_flush(s, proto_http_left(start, timeout_ms));
	if (ret != HTTP_RET_OK) {
		return ret;
	}
	s->base = 0;
	return proto_http_response_head(s, response, start, timeout_ms);
}


/**********************************************************************************************************************
 * Http interface
 **********************************************************************************************************************/

static http_ret_t http_open(Http *self, const struct http_request *request, void **session, uint32_t timeout_ms) {
	ProtoHttp *http = (ProtoHttp *)self->parent;
	if (request == NULL || request->target == NULL || session == NULL) {
		return HTTP_RET_NULL;
	}
	if ((size_t)request->method >= sizeof(proto_http_method_names) / sizeof(proto_http_method_names[0])) {
		return HTTP_RET_BAD_ARG;
	}

	/* Split the absolute target into the scheme, the authority and the path, the fragment is not sent. */
	const char *t = request->target;
	bool tls = false;
	uint16_t port = 80;
	if (!strncmp(t, "http://", 7)) {
		t += 7;
	} else if (!strncmp(t, "https://", 8)) {
		tls = true;
		port = 443;
		t += 8;
	} else if (strstr(t, "://") != NULL) {
		return HTTP_RET_NOT_IMPLEMENTED;
	} else {
		return HTTP_RET_BAD_ARG;
	}
	if ((tls && http->conf.tls == NULL) || (!tls && http->conf.tcp == NULL)) {
		return HTTP_RET_NOT_IMPLEMENTED;
	}
	const char *authority = t;
	size_t authority_len = strcspn(t, "/?#");
	const char *path = t + authority_len;
	size_t path_len = strcspn(path, "#");

	/* Spaces and control characters would break the request line or the Host header. */
	for (const char *c = authority; c < path + path_len; c++) {
		if ((unsigned char)*c <= ' ' || *c == 0x7f) {
			return HTTP_RET_BAD_ARG;
		}
	}

	/* "host[:port]" or "[IPv6 address][:port]" */
	const char *host = authority;
	size_t host_len = authority_len;
	const char *port_str = NULL;
	if (*authority == '[') {
		const char *rb = memchr(authority, ']', authority_len);
		if (rb == NULL) {
			return HTTP_RET_BAD_ARG;
		}
		host = authority + 1;
		host_len = (size_t)(rb - host);
		if (rb + 1 < path) {
			if (rb[1] != ':') {
				return HTTP_RET_BAD_ARG;
			}
			port_str = rb + 2;
		}
	} else {
		const char *colon = memchr(authority, ':', authority_len);
		if (colon != NULL) {
			host_len = (size_t)(colon - authority);
			port_str = colon + 1;
		}
	}
	if (port_str != NULL) {
		char *end = NULL;
		unsigned long p = strtoul(port_str, &end, 10);
		if (end != path || p == 0 || p > 65535) {
			return HTTP_RET_BAD_ARG;
		}
		port = (uint16_t)p;
	}
	if (host_len == 0 || host_len >= CONFIG_SERVICE_PROTO_HTTP_BUF_SIZE / 2) {
		return HTTP_RET_BAD_ARG;
	}

	/* No session slot is waited for. */
	struct proto_http_session *s = NULL;
	taskENTER_CRITICAL();
	for (size_t i = 0; i < CONFIG_SERVICE_PROTO_HTTP_MAX_SESSIONS; i++) {
		if (!http->sessions[i].used) {
			s = &http->sessions[i];
			memset(s, 0, offsetof(struct proto_http_session, buf));
			s->used = true;
			break;
		}
	}
	taskEXIT_CRITICAL();
	if (s == NULL) {
		return HTTP_RET_TIMEOUT;
	}

	/* The host name is kept at the end of the buffer for a reconnection. */
	s->limit = sizeof(s->buf) - host_len - 1;
	memcpy(s->buf + s->limit, host, host_len);
	s->buf[sizeof(s->buf) - 1] = '\0';
	s->tls = tls;
	s->port = port;
	http_ret_t cret = proto_http_connect(http, s, timeout_ms);
	if (cret != HTTP_RET_OK) {
		s->used = false;
		return cret;
	}

	/* Request line, Host and the body framing. A body of an unknown length is sent chunked, an empty body is
	 * announced only for the methods which usually carry one. */
	s->no_body = (request->method == HTTP_METHOD_HEAD);
	s->req_framing = request->has_content_length ? PROTO_HTTP_FRAMING_LENGTH : PROTO_HTTP_FRAMING_CHUNKED;
	s->req_left = request->has_content_length ? request->content_length : 0;
	s->tx_len = 0;
	bool ok = proto_http_append_head(s, proto_http_method_names[request->method]) &&
	          proto_http_append_head(s, " ") &&
	          ((path_len > 0 && path[0] == '/') || proto_http_append_head(s, "/")) &&
	          proto_http_append(s, path, path_len, 2) &&
	          proto_http_append_head(s, " HTTP/1.1\r\nHost: ") &&
	          proto_http_append(s, authority, authority_len, 2) &&
	          proto_http_append_head(s, "\r\nConnection: close\r\n");
	if (s->req_framing == PROTO_HTTP_FRAMING_CHUNKED) {
		ok = ok && proto_http_append_head(s, "Transfer-Encoding: chunked\r\n");
	} else if (s->req_left > 0 || request->method == HTTP_METHOD_POST || request->method == HTTP_METHOD_PUT ||
	           request->method == HTTP_METHOD_PATCH) {
		char cl[32];
		snprintf(cl, sizeof(cl), "Content-Length: %lu\r\n", (unsigned long)s->req_left);
		ok = ok && proto_http_append_head(s, cl);
	}
	if (!ok) {
		self->vmt->close(self, s);
		return HTTP_RET_BAD_ARG;
	}

	s->state = PROTO_HTTP_STATE_HEAD;
	*session = s;
	return HTTP_RET_OK;
}


static http_ret_t http_close(Http *self, void *session) {
	ProtoHttp *http = (ProtoHttp *)self->parent;
	if (session == NULL) {
		return HTTP_RET_NULL;
	}
	struct proto_http_session *s = proto_http_session_get(http, session);
	if (s == NULL) {
		return HTTP_RET_BAD_ARG;
	}
	proto_http_disconnect(http, s);
	s->used = false;
	return HTTP_RET_OK;
}


static http_ret_t http_add_header(Http *self, void *session, const char *name, const char *value) {
	struct proto_http_session *s = proto_http_session_get((ProtoHttp *)self->parent, session);
	if (s == NULL) {
		return HTTP_RET_BAD_ARG;
	}
	if (name == NULL || value == NULL) {
		return HTTP_RET_NULL;
	}
	if (s->state != PROTO_HTTP_STATE_HEAD) {
		return HTTP_RET_BAD_STATE;
	}
	if (name[0] == '\0' || strpbrk(name, ": \t\r\n") != NULL || strpbrk(value, "\r\n") != NULL) {
		return HTTP_RET_BAD_ARG;
	}

	/* The framing and connection headers are set by the client itself. */
	size_t name_len = strlen(name);
	if (proto_http_name_is(name, name_len, "Host") || proto_http_name_is(name, name_len, "Connection") ||
	    proto_http_name_is(name, name_len, "Content-Length") ||
	    proto_http_name_is(name, name_len, "Transfer-Encoding")) {
		return HTTP_RET_OK;
	}

	size_t mark = s->tx_len;
	if (!(proto_http_append_head(s, name) && proto_http_append_head(s, ": ") && proto_http_append_head(s, value) &&
	      proto_http_append_head(s, "\r\n"))) {
		s->tx_len = mark;
		return HTTP_RET_NOMEM;
	}
	return HTTP_RET_OK;
}


static http_ret_t http_write(Http *self, void *session, const void *buf, size_t len, uint32_t timeout_ms) {
	struct proto_http_session *s = proto_http_session_get((ProtoHttp *)self->parent, session);
	if (s == NULL) {
		return HTTP_RET_BAD_ARG;
	}
	if (buf == NULL && len > 0) {
		return HTTP_RET_NULL;
	}
	proto_http_end_head(s);
	if (s->state != PROTO_HTTP_STATE_BODY) {
		return HTTP_RET_BAD_STATE;
	}
	if (s->req_framing == PROTO_HTTP_FRAMING_LENGTH && len > s->req_left) {
		return HTTP_RET_BAD_ARG;
	}
	if (len == 0) {
		return HTTP_RET_OK;
	}

	s->body_sent = true;
	TickType_t start = xTaskGetTickCount();
	http_ret_t ret = HTTP_RET_OK;
	if (s->req_framing == PROTO_HTTP_FRAMING_CHUNKED) {
		char size[20];
		snprintf(size, sizeof(size), "%lx\r\n", (unsigned long)len);
		ret = proto_http_send(s, size, strlen(size), start, timeout_ms);
		if (ret == HTTP_RET_OK) {
			ret = proto_http_send(s, buf, len, start, timeout_ms);
		}
		if (ret == HTTP_RET_OK) {
			ret = proto_http_send(s, "\r\n", 2, start, timeout_ms);
		}
	} else {
		ret = proto_http_send(s, buf, len, start, timeout_ms);
		s->req_left -= len;
	}
	if (ret != HTTP_RET_OK) {
		s->state = PROTO_HTTP_STATE_FAILED;
	}
	return ret;
}


static http_ret_t http_get_response(Http *self, void *session, struct http_response *response,
                                    uint32_t timeout_ms) {
	struct proto_http_session *s = proto_http_session_get((ProtoHttp *)self->parent, session);
	if (s == NULL) {
		return HTTP_RET_BAD_ARG;
	}
	if (response == NULL) {
		return HTTP_RET_NULL;
	}
	proto_http_end_head(s);
	if (s->state != PROTO_HTTP_STATE_BODY ||
	    (s->req_framing == PROTO_HTTP_FRAMING_LENGTH && s->req_left > 0)) {
		return HTTP_RET_BAD_STATE;
	}

	TickType_t start = xTaskGetTickCount();
	http_ret_t ret = HTTP_RET_OK;
	if (s->req_framing == PROTO_HTTP_FRAMING_CHUNKED) {
		ret = proto_http_send(s, "0\r\n\r\n", 5, start, timeout_ms);
	}

	/* A request without a body is still whole in the buffer, keep it to be repeated with the credentials. */
	ProtoHttp *http = (ProtoHttp *)self->parent;
	s->base = (http->conf.username != NULL && !s->body_sent && s->req_framing == PROTO_HTTP_FRAMING_LENGTH) ?
	          s->tx_len : 0;
	if (ret == HTTP_RET_OK) {
		ret = proto_http_flush(s, proto_http_left(start, timeout_ms));
	}
	if (ret == HTTP_RET_OK) {
		ret = proto_http_response_head(s, response, start, timeout_ms);
	}
	if (ret == HTTP_RET_OK && response->status == 401 && s->base > 0 && s->basic_auth) {
		ret = proto_http_auth_retry(http, s, response, start, timeout_ms);
	}
	s->state = (ret == HTTP_RET_OK) ? PROTO_HTTP_STATE_RESPONSE_HEAD : PROTO_HTTP_STATE_FAILED;
	return ret;
}


static http_ret_t http_read_header(Http *self, void *session, char *name, size_t name_size, char *value,
                                   size_t value_size) {
	struct proto_http_session *s = proto_http_session_get((ProtoHttp *)self->parent, session);
	if (s == NULL) {
		return HTTP_RET_BAD_ARG;
	}
	if (name == NULL || value == NULL) {
		return HTTP_RET_NULL;
	}
	if (s->state != PROTO_HTTP_STATE_RESPONSE_HEAD) {
		return HTTP_RET_BAD_STATE;
	}

	/* The kept lines are "<name>:<value>", skip the headers not fitting the buffers. */
	while (s->hdr_pos < s->hdr_end) {
		const char *line = s->buf + s->hdr_pos;
		s->hdr_pos += strlen(line) + 1;
		const char *colon = strchr(line, ':');
		size_t name_len = (size_t)(colon - line);
		const char *v = colon + 1;
		while (*v == ' ' || *v == '\t') {
			v++;
		}
		size_t value_len = strlen(v);
		while (value_len > 0 && (v[value_len - 1] == ' ' || v[value_len - 1] == '\t')) {
			value_len--;
		}
		if (name_len >= name_size || value_len >= value_size) {
			continue;
		}
		memcpy(name, line, name_len);
		name[name_len] = '\0';
		memcpy(value, v, value_len);
		value[value_len] = '\0';
		return HTTP_RET_OK;
	}
	return HTTP_RET_EOF;
}


static http_ret_t http_read(Http *self, void *session, void *buf, size_t size, size_t *len, uint32_t timeout_ms) {
	struct proto_http_session *s = proto_http_session_get((ProtoHttp *)self->parent, session);
	if (s == NULL || size == 0) {
		return HTTP_RET_BAD_ARG;
	}
	if (buf == NULL || len == NULL) {
		return HTTP_RET_NULL;
	}
	*len = 0;
	if (s->state == PROTO_HTTP_STATE_RESPONSE_HEAD) {
		/* The unread headers are dropped, their space is reused for the chunk lines. */
		s->state = PROTO_HTTP_STATE_RESPONSE_BODY;
		s->base = 0;
		s->hdr_pos = 0;
		s->hdr_end = 0;
	}
	if (s->state != PROTO_HTTP_STATE_RESPONSE_BODY) {
		return HTTP_RET_BAD_STATE;
	}

	TickType_t start = xTaskGetTickCount();
	while (!s->done) {
		if (s->resp_framing == PROTO_HTTP_FRAMING_CHUNKED && s->body_left == 0) {
			http_ret_t ret = proto_http_chunk_line(s, start, timeout_ms);
			if (ret != HTTP_RET_OK) {
				s->state = (ret == HTTP_RET_TIMEOUT) ? s->state : PROTO_HTTP_STATE_FAILED;
				return ret;
			}
			continue;
		}
		if (s->resp_framing == PROTO_HTTP_FRAMING_LENGTH && s->body_left == 0) {
			s->done = true;
			break;
		}

		size_t n = size;
		if (s->resp_framing != PROTO_HTTP_FRAMING_CLOSE && n > s->body_left) {
			n = s->body_left;
		}
		stream_ret_t ret = proto_http_recv(s, buf, n, len, proto_http_left(start, timeout_ms));
		if (ret == STREAM_RET_EOF && s->resp_framing == PROTO_HTTP_FRAMING_CLOSE) {
			s->done = true;
			break;
		}
		if (ret != STREAM_RET_OK) {
			s->state = (ret == STREAM_RET_TIMEOUT) ? s->state : PROTO_HTTP_STATE_FAILED;
			return proto_http_stream_ret(ret);
		}
		if (s->resp_framing != PROTO_HTTP_FRAMING_CLOSE) {
			s->body_left -= *len;
			s->chunk_crlf = (s->resp_framing == PROTO_HTTP_FRAMING_CHUNKED && s->body_left == 0);
		}
		return HTTP_RET_OK;
	}
	return HTTP_RET_EOF;
}


static const struct http_vmt proto_http_vmt = {
	.open = http_open,
	.close = http_close,
	.add_header = http_add_header,
	.write = http_write,
	.get_response = http_get_response,
	.read_header = http_read_header,
	.read = http_read,
};


/**********************************************************************************************************************
 * Service API
 **********************************************************************************************************************/

proto_http_ret_t proto_http_init(ProtoHttp *self, const struct proto_http_conf *conf) {
	if (u_assert(self != NULL) ||
	    u_assert(conf != NULL)) {
		return PROTO_HTTP_RET_NULL;
	}
	memset(self, 0, sizeof(ProtoHttp));
	memcpy(&self->conf, conf, sizeof(struct proto_http_conf));

	self->http.vmt = &proto_http_vmt;
	self->http.parent = self;
	self->http.desc = &proto_http_desc;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("init ok"));
	return PROTO_HTTP_RET_OK;
}


proto_http_ret_t proto_http_free(ProtoHttp *self) {
	if (u_assert(self != NULL)) {
		return PROTO_HTTP_RET_NULL;
	}
	return PROTO_HTTP_RET_OK;
}


proto_http_ret_t proto_http_start(ProtoHttp *self) {
	if (u_assert(self != NULL)) {
		return PROTO_HTTP_RET_NULL;
	}
	if (self->conf.tcp == NULL && self->conf.tls == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("neither Tcp nor Tls is configured"));
		return PROTO_HTTP_RET_FAILED;
	}
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("started"));
	return PROTO_HTTP_RET_OK;
}


proto_http_ret_t proto_http_stop(ProtoHttp *self) {
	if (u_assert(self != NULL)) {
		return PROTO_HTTP_RET_NULL;
	}
	return PROTO_HTTP_RET_OK;
}
