# pynbus2

A small Python client library for the [plumCore](https://krtko.org) **nbus2** messaging bus.

nbus2 nodes are addressed like UDP: a 32-bit service ID (sid) and a 4-bit endpoint (ep) play the
role of an IP address and a port. `pynbus2` lets you select *how* you reach the bus with a URI, open
sockets to individual services, and read/write datagrams on them.

## Install

```sh
cd tools/pynbus2
pdm install          # for development
# or build a source distribution:
pdm build            # produces dist/pynbus2-<version>.tar.gz
```

The UDP/IPv6 transport is pure standard library. The serial transport additionally needs
`pyserial`; install it with the `serial` extra: `pip install pynbus2[serial]` (or `pdm install -G serial`).

## Usage

```python
import pynbus2

# Connect to the nbus2 network via the UDP/IPv6 bridge.
with pynbus2.connect('udp6://') as nbus:
    sock = nbus.socket('00010002', 3)      # connect to sid 0x00010002, endpoint 3
    sock.send(b'hello')
    reply = sock.recv(timeout=0.5)         # None on timeout

    # Request/response with retransmit (what the command tools use):
    reply = sock.request(b'ping', timeout=0.05, retries=50)
```

A default destination can also be carried in the URI path, so `nbus.socket()` hands back an
already-connected socket:

```python
with pynbus2.connect('udp6:///00010002/3') as nbus:
    sock = nbus.socket()
```

Payloads are raw `bytes`; the application chooses the encoding (the plumCore tools use CBOR).

## Connection URIs

The URI scheme selects the transport.

### `udp6://` — UDP/IPv6 bridge

```
udp6://[<local-bind-addr>][:<base-port>][/<sid>[/<ep>]][?prefix=...&self=...&mtu=...]
```

| part / query | meaning | default |
|--------------|---------|---------|
| host         | local IPv6 bind address | derived from `self` and `prefix` |
| port         | base UDP port; the remote port is `base + ep` | `52000` |
| `/sid/ep`    | default destination service | none |
| `prefix`     | IPv6 prefix; a service's address is the prefix with the sid in its low 32 bits | `fd00:dead:beef::` |
| `self`       | our own service ID | `00000001` |
| `mtu`        | receive buffer size in bytes | `1024` |

Examples:

```
udp6://                                  all defaults
udp6://[fd00:dead:beef::1]:52000         explicit bind address and base port
udp6:///00010002/3                       default bridge, default destination 00010002 ep 3
udp6://?prefix=fd00:dead:beef::&self=1   override the prefix and our own service ID
```

The bridge (and the device behind it) perform the nbus2 wire framing, medium access and packet
protection, so this transport carries datagram payloads verbatim and does no cryptography itself.

### `dgtext+serial://` — serial line, dgtext framed

Talks the nbus2 wire protocol directly over a serial port. Packets are framed as printable `dgtext`
lines (`$<base64>*<blake2s-checksum>\n`, matching the firmware `proto-dgtext` service) and protected
with the **BLAKE2s-SIV** scheme (`hashlib` only — no third-party crypto). Needs the `serial` extra.

```
dgtext+serial://<device>[/<sid>/<ep>][?baudrate=...&key=...&self=...]
```

| part / query | meaning | default |
|--------------|---------|---------|
| device       | serial device path (the leading part of the URI path, e.g. `/dev/ttyUSB0`) | — (required) |
| `/sid/ep`    | default destination service, as the last two path components | none |
| `baudrate`   | serial baud rate | `115200` |
| `key`        | preshared key for the nbus2 packet protection | `abcd` |
| `self`       | our own service ID, used as the source for unbound sockets | `00000001` |

The device occupies the path, so a default destination is carried as the last two path components
after it. They are only treated as a destination when they parse as a sid and an endpoint, so a deep
device path (e.g. `/dev/serial/by-id/...`) without a trailing destination is left intact.

```python
# Destination in the URI path; nbus.socket() returns it already connected.
with pynbus2.connect('dgtext+serial:///dev/ttyUSB0/00010002/3?baudrate=1000000') as nbus:
    sock = nbus.socket()
    reply = sock.request(b'ping')

# Or give the device only and connect explicitly.
with pynbus2.connect('dgtext+serial:///dev/ttyUSB0') as nbus:
    sock = nbus.socket('00010002', 5)
    reply = sock.request(b'ping')
```

A background reader thread decrypts and demultiplexes incoming packets to the matching sockets, so
several sockets can share one serial line.

> The host-side BLAKE2s-SIV protection interoperates with the device only when the firmware emits
> that scheme. The legacy ChaCha20+halfsiphash scheme is not decoded host-side (no standard library
> provides ChaCha20-128); `decode` is built to try schemes newest-first so it can be added later.

## Extending

New transports subclass `pynbus2.transport.base.Transport` / `SocketBackend` and register for a
scheme via `pynbus2.register(scheme, TransportClass)`. A direct serial transport (dgstream/dgtext)
would additionally need the nbus2 wire cipher (ChaCha20-128 + halfsiphash) and is not implemented.
