# Remote interface BLE transport

The `remote-interface-ble` service exports all [RemoteInterface](../interfaces/remote-interface.md) instances
advertised in the service locator (`ISERVICELOCATOR_TYPE_REMOTE_INTERFACE`) over a single BLE GATT service. A BLE
central discovers the interfaces, reads their descriptions and opens sessions on them through a control
characteristic, and exchanges the messages through a data characteristic multiplexing all open sessions.


## Why a multiplexed layout

The GATT layout is fixed: two characteristics, whatever the number of exported interfaces. Mapping each interface to
its own characteristic does not work well with the ST67W611 module (and BLE modules in general):

- only a single GATT service reliably routes peer writes, and it holds a limited number of characteristics
- a characteristic following a read-only one never reports writes, so no read-only characteristic is used
- the GATT table is fixed when the server is registered, an interface advertised later could never be exported
- a single characteristic value is limited to 244 bytes, a descriptor of many interfaces would not fit

Interfaces are instead identified by the name they were advertised with and resolved when a request arrives.


## GATT layout

All UUIDs are 128 bit. The first 15 bytes are the first 15 bytes of `sha256("RemoteInterfaceBle")`, the last byte
selects the attribute:

| Attribute | UUID                                   | Properties          |
|-----------|----------------------------------------|---------------------|
| service   | `e146becd-ab61-d423-6586-22a4b435e700` | primary             |
| control   | `e146becd-ab61-d423-6586-22a4b435e701` | read, write, notify |
| data      | `e146becd-ab61-d423-6586-22a4b435e702` | read, write, notify |


## Control protocol

The central writes a single request to the control characteristic, the device answers with a single notification
on the same characteristic. Requests and responses are CBOR maps following the `proto-conf` conventions: the
request carries the command in the `"c"` key, the response contains only the result fields. On error the response
contains an `"err"` text string, a command with no other result returns `"ret": "ok"`. Neither a request nor a
response is fragmented, each must fit a single characteristic value (ATT MTU minus 3 bytes).

| Command | Request | Response              | Description                                                                |
|---------|---------|-----------------------|----------------------------------------------------------------------------|
| `walk`  | `n`     | `{n}`                 | Name of the interface following `n`, see below                             |
| `desc`  | `n`     | `{p, pv, mtu, ms}`    | Description of the interface `n`                                           |
| `open`  | `n`     | `{ret:"ok", ch}`      | Open a session on the interface `n`, bound to the data channel `ch`        |
| `close` | `ch`    | `{ret:"ok"}`          | Close the session on the data channel `ch` and free the channel            |

| Key   | Meaning                                                                                     |
|-------|---------------------------------------------------------------------------------------------|
| `c`   | command                                                                                     |
| `n`   | interface name as advertised in the service locator, `""` is the root interface            |
| `p`   | protocol name (`desc->protocol`)                                                            |
| `pv`  | protocol version (`desc->protocol_version`), omitted if not versioned                       |
| `mtu` | maximum message size (`desc->mtu`)                                                          |
| `ms`  | maximum number of concurrent sessions (`desc->max_sessions`)                                |
| `ch`  | data channel number, 1 to `CONFIG_SERVICE_REMOTE_INTERFACE_BLE_CHANNELS`                    |
| `err` | error text: `missing name`, `missing channel`, `not found`, `busy`, `no channel`, `open failed`, `not supported` |
| `ret` | `"ok"` on success                                                                           |

The interfaces are walked in the order they are discovered in the service locator, the tree structure of their
(space-delimited) names is not interpreted. A `walk` with `n` null (or absent) returns the first interface, the
response following the last one has `n` null. The root interface (`""`) may appear anywhere during the walk.
Interfaces advertised without a name are not exported.

```
→ {"c":"walk", "n":null}           ← {"n":"flash"}
→ {"c":"walk", "n":"flash"}        ← {"n":""}
→ {"c":"walk", "n":""}             ← {"n":"card1 flash"}
→ {"c":"walk", "n":"card1 flash"}  ← {"n":null}
→ {"c":"desc", "n":"card1 flash"}  ← {"p":"flash", "pv":"1.0.0", "mtu":512, "ms":1}
→ {"c":"open", "n":"card1 flash"}  ← {"ret":"ok", "ch":1}
→ {"c":"close", "ch":1}            ← {"ret":"ok"}
```

`open` does not wait, `busy` is returned if the interface has no free session slot or is locked by another session.
The same interface may be opened several times, each `open` gets its own session and channel.


## Data framing

Each value written to or notified on the data characteristic starts with a header byte followed by a message
fragment:

| Bits | Name  | Meaning                                  |
|------|-------|------------------------------------------|
| 7    | START | first fragment of a message              |
| 6    | END   | last fragment of a message               |
| 5..0 | CH    | data channel                             |

A message is sent as consecutive fragments of a single channel, fragments of different messages are never
interleaved. A message fitting a single value has both START and END set. Notifications carry at most ATT MTU minus
4 bytes of message (at most 243 bytes). A fragment not continuing the message being reassembled or a message bigger
than the interface MTU is dropped, as the RemoteInterface delivery is best effort.


## Session lifetime

All channels are closed when the central disconnects.

When the interface closes a session, the device sends the messages still queued, then an empty message (a single
header byte with START and END) on that channel, and closes the session right away. The channel stays reserved
(messages written to it are dropped) until the central sends an explicit `close`, so it is never reused while the
central may still consider it open.


## Configuration

The service is enabled by `CONFIG_SERVICE_REMOTE_INTERFACE_BLE`.

| Option                                         | Meaning                                                   |
|------------------------------------------------|-----------------------------------------------------------|
| `CONFIG_SERVICE_REMOTE_INTERFACE_BLE_CHANNELS` | number of data channels (concurrently open sessions)      |
| `CONFIG_SERVICE_REMOTE_INTERFACE_BLE_RX_QUEUE_LEN` | number of inbound data fragments buffered             |
| `CONFIG_SERVICE_REMOTE_INTERFACE_BLE_DEBUG`    | debug logging of control requests and data traffic        |

The runtime configuration is passed to `remote_interface_ble_init` in a `struct remote_interface_ble_conf`:

| Field          | Meaning                                                                                   |
|----------------|-------------------------------------------------------------------------------------------|
| `ble`          | BLE device the GATT service is built on                                                   |
| `event_cb`     | optional callback the BLE events are forwarded to (the service owns the device callback) |
| `event_cb_ctx` | context passed to `event_cb`                                                              |

The message buffers are sized to the largest MTU of the interfaces advertised when `remote_interface_ble_start` is
called. Interfaces advertised later are exported too, but `open` fails for those with a bigger MTU.


## Usage

```c
RemoteInterfaceBle ri_ble;

remote_interface_ble_init(&ri_ble, &(const struct remote_interface_ble_conf) {
	.ble = ble,
	.event_cb = app_ble_event_handler,
	.event_cb_ctx = app,
});
/* All RemoteInterface instances are already advertised in the service locator. */
remote_interface_ble_start(&ri_ble);
ble->vmt->server_start(ble);
ble->vmt->advertising_start(ble);
```

The service takes over the single event callback of the BLE device, the same as `proto-dgble`. Both can't be used
on the same device at the same time.
