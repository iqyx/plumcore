# HTTP interface

`Http` (`<interfaces/http.h>`) abstracts a single HTTP request/response exchange. It is deliberately symmetric, the
party which produces responses implements it and the party which has a request calls it:

| Implementer | Caller | Request target |
|-------------|--------|----------------|
| route service (REST API, file server, firmware upload) | HTTP server service | origin-form, `/api/v1/conf?x=1` |
| network module driver with an internal stack (AT-command Wi-Fi/cellular) | application | absolute URL, `http://host:8080/path` |

Because both roles share the interface, services can be chained. An HTTP server can serve routes provided by any
number of route services, an application can perform requests through a module driver or through a future client
service running on top of a local TCP/IP stack without any change, and a proxy can implement `Http` by forwarding to
another `Http` instance.

The interface carries only the semantics of HTTP. Connection handling, request parsing, `Host`, `Connection`,
keep-alive, chunked transfer coding and TLS belong to the server or the client implementation and never reach the
other side.


## Sessions

Each request is processed in its own session. The implementation owns `desc->max_sessions` preallocated session
slots, `open()` returns an opaque session pointer and `close()` frees it. A server with several TCP connections opens
one session per request in flight, a module driver typically supports only one or a few concurrent requests.


## Session state machine

```
open() ──► headers ──► body ──► response ──► response headers ──► response body ──► close()
            add_header()  write()   get_response()  read_header()         read()
```

1. `open()` takes the method, the target and the request body framing (`struct http_request`).
2. `add_header()` passes request headers, zero or more times.
3. `write()` passes the request body in chunks of any size, zero or more times. If the body length was announced in
   `open()`, exactly that many bytes are written.
4. `get_response()` ends the request and waits for the status code and the response body framing
   (`struct http_response`).
5. `read_header()` returns response headers one by one until `HTTP_RET_EOF`. Optional, unread headers are skipped.
6. `read()` returns the response body in chunks until `HTTP_RET_EOF`.
7. `close()` frees the session. It may be called in any state and must be called exactly once for every opened
   session, even if a previous method failed. Closing an unfinished session aborts the request.

Calling a method out of order returns `HTTP_RET_BAD_STATE`.


## Framing

Body framing is not passed as headers. `Content-Length` and `Transfer-Encoding` are represented by the
`has_content_length`/`content_length` fields of `struct http_request` and `struct http_response`:

- A server parsing a request with `Content-Length` announces it in `open()`. For a chunked request it decodes the
  chunks and announces an unknown length.
- A server forwarding a response with an unknown length uses chunked transfer coding or closes the connection after
  the body.
- A client implementation which needs the body length in advance (most AT-command modules) returns
  `HTTP_RET_NOT_IMPLEMENTED` from `open()` when it is not known.


## Routing

A route service returns `HTTP_RET_NOT_FOUND` from `open()` for targets it does not serve. A server holding several
route services offers the request to each of them in turn and responds `404 Not Found` if none accepts it. Once
a route service accepts the target, all further errors it detects (malformed content, unsupported method for the
resource, missing data) are reported as regular responses with the corresponding status code, not as return codes.
Return codes describe failures of the exchange itself (timeouts, lost connections, exhausted resources).


## Memory

The interface streams everything: headers one at a time, bodies in caller-sized chunks. Neither side has to buffer
a whole request or response. A route service keeps only the request headers it needs and ignores the rest
(`add_header()` returns `HTTP_RET_OK` for ignored headers). A client implementation may return only the response
headers its module reports. All buffers are owned by the caller and may be reused as soon as a method returns.


## Context and locking

All methods block up to their `timeout_ms` and run in the caller's task context. A route service processes the
request in the server's task, either progressively in `write()` (eg. streaming a firmware image to flash) or at once
in `get_response()`. Implementations are responsible for locking, different sessions may be used from different
tasks concurrently. A single session must not be used from more than one task at a time.


## Example

Performing a request through a module driver:

```c
struct http_request req = {
	.method = HTTP_METHOD_POST,
	.target = "http://example.com/api/v1/data",
	.has_content_length = true,
	.content_length = len,
};
void *session = NULL;
if (http->vmt->open(http, &req, &session, 5000) != HTTP_RET_OK) {
	return APP_RET_FAILED;
}

struct http_response resp = {0};
if (http->vmt->add_header(http, session, "Content-Type", "application/json") != HTTP_RET_OK ||
    http->vmt->write(http, session, data, len, 5000) != HTTP_RET_OK ||
    http->vmt->get_response(http, session, &resp, 10000) != HTTP_RET_OK) {
	http->vmt->close(http, session);
	return APP_RET_FAILED;
}

uint8_t buf[128];
size_t rlen = 0;
while (http->vmt->read(http, session, buf, sizeof(buf), &rlen, 5000) == HTTP_RET_OK) {
	/* process rlen bytes of the response body */
}
http->vmt->close(http, session);
```

The instance is discovered through the service locator as `ISERVICELOCATOR_TYPE_HTTP`.
