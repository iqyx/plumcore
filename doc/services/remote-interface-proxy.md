# Remote interface proxy

The `remote-interface-proxy` service exports legacy protocol handlers, which serve a single peer over a `Datagram`
interface, as a [RemoteInterface](../interfaces/remote-interface.md). Transports (an nbus2 exporter, a BLE GATT
exporter, a framed serial line, ...) can then use the handlers like any other RemoteInterface without the handlers
being rewritten.


## Why it exists

A RemoteInterface is transport agnostic and multi-session: a transport opens a session for every peer it serves, and a
single interface may be exported over several transports at once. All protocol handlers implemented so far
(`proto-conf`, `nbus-flash`, `nbus-mq-poll`, ...) predate this model. Each of them:

- talks to exactly one `Datagram` interface, usually an nbus2 socket or a `proto-dgble` endpoint the application wires
  to it directly
- keeps its protocol state in the instance, so it can serve only one peer at a time
- has no notion of sessions, opening or closing

The proxy sits between the two worlds. Toward the transports it is a single RemoteInterface with a number of session
slots. Toward the handlers it is a set of `Datagram` interfaces, one per session slot. The application creates one
handler instance per slot, so every session gets its own private protocol state, exactly as the RemoteInterface
contract requires.

```
              transports                       remote-interface-proxy                legacy handlers

  nbus2 exporter ─┐                        ┌─ session slot 0 ── Datagram ─── handler instance 0
                  ├── RemoteInterface ─────┤
  BLE exporter ───┘    (proxy.iface)       └─ session slot 1 ── Datagram ─── handler instance 1
```


## How it works

The proxy has no task of its own. Each session slot holds two FreeRTOS message buffers, one for each direction, each
able to hold `queue_len` messages of up to `desc.mtu` bytes.

- **open** waits up to the given timeout for a free slot (a counting semaphore tracks them), marks it used and stores
  the transport's Notify interface.
- **write** (transport) puts the message into the slot's receive buffer, waiting up to the given timeout for space.
  The handler picks it up with its `Datagram` read, which blocks indefinitely.
- `Datagram` **write** (handler) puts the response into the slot's transmit buffer and calls the transport's Notify
  with the number of messages waiting. It never blocks: if the slot has no open session or the buffer is full, the
  message is dropped. A handler therefore never stalls on a transport which stopped reading.
- **read** (transport) takes a message from the transmit buffer, waiting up to the given timeout.
- **close** drops all pending messages in both directions and frees the slot.

A handler wired to a free slot simply stays blocked in its `Datagram` read until a session is opened and the first
message arrives. Address metadata (`struct datagram_msg`) is zeroed on read and ignored on write, as the addressing
belongs to the transport.

As a legacy handler cannot close a session, the proxy never returns `REMOTE_INTERFACE_RET_CLOSED`. Sessions are closed
only by the transport.


## Configuration

The proxy is enabled by `CONFIG_SERVICE_REMOTE_INTERFACE_PROXY`. Everything else is passed to
`remote_interface_proxy_init` in a `struct remote_interface_proxy_conf`, which is copied into the instance:

| Field               | Meaning                                                                                        |
|---------------------|------------------------------------------------------------------------------------------------|
| `desc`              | Description of the exported interface, advertised by the transports as is                      |
| `desc.protocol`     | Protocol name spoken by the handlers (eg. `"conf"`), mandatory                                  |
| `desc.protocol_version` | Protocol version, NULL if not versioned                                                    |
| `desc.mtu`          | Maximum message size in both directions, at most the handler's own buffer size                 |
| `desc.max_sessions` | Number of session slots, therefore also the number of `Datagram` interfaces and handler instances |
| `desc.session_idle_ms` | Inactivity timeout for connectionless transports, 0 to keep sessions until the peer is lost |
| `queue_len`         | Number of messages buffered in each direction of a session                                      |

The strings in `desc` are borrowed, they must outlive the proxy instance.

All resources are allocated in `remote_interface_proxy_init`. When it returns, both the RemoteInterface
(`proxy.iface`) and the `Datagram` interfaces of all session slots (`proxy.sessions[i].dgram`) are ready to be used.
The memory used is roughly `2 * max_sessions * queue_len * (mtu + 4)` bytes of message buffers plus whatever the
handler instances need.


## Usage

1. Initialize the proxy with the interface description and the number of sessions.
2. Create one protocol handler instance per session slot and wire it to `proxy.sessions[i].dgram`.
3. Advertise `proxy.iface` in the service locator as `ISERVICELOCATOR_TYPE_REMOTE_INTERFACE`. Transports discover it
   there and export it.


## Example: proto-conf

The following example exports the configuration tree access protocol (`proto-conf`) to two concurrent peers. Each
peer gets its own `proto-conf` instance, so one peer walking the tree does not disturb the other.

```c
#include <remote-interface-proxy.h>
#include <proto-conf.h>

#define CONF_SESSIONS 2

RemoteInterfaceProxy conf_proxy;
ProtoConf conf_handlers[CONF_SESSIONS];

void app_export_conf(Conf *root) {
	if (remote_interface_proxy_init(&conf_proxy, &(const struct remote_interface_proxy_conf) {
		.desc = {
			.protocol = "conf",
			.protocol_version = "1.0.0",
			/* proto-conf receives into a buffer of this size */
			.mtu = CONFIG_SERVICE_PROTO_CONF_MAX_DATAGRAM_LEN,
			.max_sessions = CONF_SESSIONS,
			.session_idle_ms = 30000,
		},
		.queue_len = 2,
	}) != REMOTE_INTERFACE_PROXY_RET_OK) {
		return;
	}

	/* One proto-conf instance per session slot, each serving the same configuration tree. */
	for (size_t i = 0; i < CONF_SESSIONS; i++) {
		proto_conf_init(&conf_handlers[i], &(const struct proto_conf_conf) {
			.d = &conf_proxy.sessions[i].dgram,
			.root = root,
		});
	}

	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_REMOTE_INTERFACE, (Interface *)&conf_proxy.iface, "conf");
}
```

`proto-conf` starts its task in `proto_conf_init`. Until a transport opens a session, both tasks wait in their
`Datagram` read. When a transport opens a session, it gets one of the two slots. Its requests reach the `proto-conf`
instance wired to that slot, and the responses come back through the same slot.

Keep in mind that the handler instances are independent of each other but still share the resources they are given.
Here both instances access the same `Conf` tree, so a write by one peer is immediately visible to the other.
