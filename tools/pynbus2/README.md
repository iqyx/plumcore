# pynbus2

A small Python client library for the [plumCore](https://krtko.org) **nbus2** messaging bus.

nbus2 nodes are addressed like UDP: a 32-bit service ID (sid) and a 4-bit endpoint (ep) play the
role of an IP address and a port. `pynbus2` lets you select *how* you reach the bus with a URI, open
sockets to individual services, and read/write datagrams on them.

## Install

```sh
cd tools/pynbus2
pdm install -G :all  # for development, with all the transports
pdm run pytest       # run the tests
# or build the distributions:
pdm build            # produces dist/pynbus2-<version>.tar.gz and the wheel
```

The core library and the UDP/IPv6 transport are pure standard library. The other transports need
optional dependencies, installed with an extra (`pip install pynbus2[<extra>]`, or
`pdm install -G <extra>`):

| extra    | transports                          | installs           |
|----------|-------------------------------------|--------------------|
| `serial` | `dgtext+serial`, `dgstream+serial`  | pyserial, cbor2    |
| `ble`    | `dgble`, `rible`                    | SimplePyBLE ≥1.2, cbor2 |
| `swd`    | `swd`                               | pyocd, cbor2       |
| `ssh`    | `ssh`                               | paramiko, cbor2    |
| `proxy`  | the `pynbus2-ssh-proxy` service     | cbor2              |
| `mq`     | the `pynbus2.mq` message queue client | cbor2            |
| `all`    | all of the above                    |                    |

Another project can depend on the built sdist directly, e.g. with pdm:

```sh
pdm add "pynbus2[serial,swd] @ file:///path/to/plumcore/tools/pynbus2/dist/pynbus2-0.2.0.tar.gz"
```

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
several sockets can share one serial line. Multicast packets are never delivered to sockets. The
descriptor advertisements services multicast every couple of seconds are logged at DEBUG level and used
for discovery (see below).

> The host-side BLAKE2s-SIV protection interoperates with the device only when the firmware emits
> that scheme. The legacy ChaCha20+halfsiphash scheme is not decoded host-side (no standard library
> provides ChaCha20-128); `decode` is built to try schemes newest-first so it can be added later.

### `dgstream+serial://` — serial line, framed by idle gaps

The same as `dgtext+serial://`, with the same URI form and query parameters, but framed as the firmware
`proto-dgstream` service does: each datagram is written verbatim in a single burst and a short idle
period on the line (20 ms) delimits it from the next one. Suits half-duplex links (RS485, CAN PHY).

```
dgstream+serial://<device>[/<sid>/<ep>][?baudrate=...&key=...&self=...]
```

### `dgble://` — BLE GATT tunnel (proto-dgble)

Tunnels nbus2 datagrams over the firmware `services/proto-dgble` GATT service: one primary service per
service ID, one readable/writable/notifiable characteristic per endpoint. A datagram to an endpoint is
a write to its characteristic; a datagram from the device is a notification on it. Payloads are carried
verbatim, so no nbus2 wire crypto happens host-side. Needs the `ble` extra (SimplePyBLE, sync API).

```
dgble://<device-name-or-mac>/<sid>/<ep>[?scan=...&adapter=...]
```

| part / query | meaning | default |
|--------------|---------|---------|
| host         | device to connect to, matched against its advertised name or MAC address | — (required) |
| `/sid/ep`    | destination service ID and endpoint (both required) | — (required) |
| `scan`       | scan duration in milliseconds | `5000` |
| `adapter`    | Bluetooth adapter to use, by identifier or address | first available |

On connect the transport scans for the device, connects, enumerates its GATT services and verifies the
selected service ID (SID) is present (its 128-bit UUID is built from the vendor prefix
`a3def8c0-acdd-a602-e3a1-78`, the `0x90` service group and the 32-bit SID). Both path components are
mandatory, so `nbus.socket()` always returns an already-connected socket.

```python
# By MAC address:
with pynbus2.connect('dgble://AA:BB:CC:DD:EE:FF/00010002/3') as nbus:
    sock = nbus.socket()
    reply = sock.request(b'ping')

# By advertised name, with a longer scan:
with pynbus2.connect('dgble://nwdaq-hh1/00010002/3?scan=8000') as nbus:
    sock = nbus.socket()
    reply = sock.request(b'ping')
```

Endpoint numbers are 16-bit (matching proto-dgble), unlike the 4-bit nbus2 endpoints of the other
transports.

### `rible://` — RemoteInterface over BLE (remote-interface-ble)

Opens a session on a RemoteInterface exported by the firmware `services/remote-interface-ble` service.
The device exposes a single GATT service with a control and a data characteristic. The interface is
selected by the name it was advertised with in the firmware service locator, sessions are opened and
closed through the control characteristic and the messages are fragmented over the data
characteristic. Needs the `ble` extra (SimplePyBLE, sync API, cbor2).

```
rible://<device-name-or-mac>[/<path>/<to>/<interface>][?adapter=...]
```

| part / query | meaning | default |
|--------------|---------|---------|
| host         | bonded device to connect to, matched against its name or MAC address | — (required) |
| path         | interface name, the path components are joined with spaces (`/card1/flash` is `"card1 flash"`) | discover the interfaces |
| `adapter`    | Bluetooth adapter to use, by identifier or address | first available |

On connect the transport checks the GATT service is present, queries the interface with the `desc`
control command and logs its description (protocol, version, MTU, maximum sessions). `nbus.socket()`
opens a session on the interface and returns it already connected, closing the socket closes the
session.

```python
with pynbus2.connect('rible://nwdaq-hh1/conf') as nbus:
    sock = nbus.socket()
    reply = sock.request(cbor2.dumps({'c': 'stat', 'p': []}))
```

Without a path the transport walks all interfaces exported by the device and logs their descriptions.
The root interface `""`, if the device has one, becomes the default destination, otherwise
`nbus.socket()` returns an unconnected socket. The same list is returned by `nbus.walk()` as
`(name, desc)` tuples on any `rible://` or `swd://` connection (other transports raise `NbusError`):

```python
with pynbus2.connect('rible://nwdaq-hh1') as nbus:
    for name, desc in nbus.walk():
        print(repr(name), desc['p'], desc.get('pv'), desc['mtu'], desc['ms'])
```

`tools/proto-conf.py rible://nwdaq-hh1 --list` prints the same list.

The service UUIDs are the first 15 bytes of `sha256("RemoteInterfaceBle")` followed by an attribute
selector: `…e700` service, `…e701` control, `…e702` data.

### `swd://` — debug probe, virtual services

Drives the target directly through an SWD debug probe (pyocd), without any firmware running. The
transport serves *virtual* interfaces on the host instead, implemented on top of the debug access, and
they are used exactly like interfaces reached over the other transports. Like `rible://`, they are
selected by name, the path components are joined with spaces. Today the only interface is `flash`: the
target's raw flash exported through the standard flash protocol (the firmware `services/nbus-flash`
protocol, see `pynbus2.services.flash`). Every pyocd flash region becomes a volume, named `flash0`,
`flash1`, ... in address order. Regions which only alias another one (e.g. the secure alias of the flash
on TrustZone devices) are skipped. `nbus.walk()` lists the interfaces. Needs the `swd` extra.

```
swd://<probe-unique-id>[/<path>/<to>/<interface>][?target=...&pack=...&frequency=...]
```

| part / query | meaning | default |
|--------------|---------|---------|
| host         | unique ID of the debug probe, a case-insensitive substring match | — (required) |
| path         | interface name, the path components are joined with spaces | `flash` |
| `target`     | pyocd target type | autodetected by pyocd |
| `pack`       | path to the CMSIS pack providing the target and its flash algorithm | none |
| `frequency`  | SWD clock frequency in Hz | pyocd default |

`tools/proto-nbus-flash.py` works over it unchanged, e.g. to recover a target with a broken
bootloader:

```sh
tools/proto-nbus-flash.py 'swd://066DFF555187534867173540?target=stm32g491ccux&pack=Keil.STM32G4xx_DFP.2.0.0.pack' --list
```

The target is reset and halted when its flash is first erased or programmed (the flash algorithm runs
in the target RAM), the flash `reset` command or closing the connection resumes it.

### `ssh://` — remote transports over SSH (pynbus2-ssh-proxy)

Reaches a transport of a remote host, typically a Linux DAQ controller with a plumCore device attached,
through the `pynbus2-ssh-proxy` running there as the login shell of a dedicated user (see
[SSH proxy](#ssh-proxy)). The proxy serves the one transport it was started with (`-t`), so the remote URI
carries no scheme. The SSH connection is made with paramiko, needs the `ssh` extra.

The host name, user, port and identity files of a host alias are taken from `~/.ssh/config`, the keys of the
SSH agent and those in `~/.ssh` are tried. The host key must be in `~/.ssh/known_hosts`, a changed one is
always refused. Without a terminal to ask on, the environment answers what the SSH client would ask:

| variable                    | meaning |
|-----------------------------|---------|
| `PYNBUS2_SSH_PASSWORD`      | the password, or the passphrase of a private key, used when one is needed |
| `PYNBUS2_SSH_TRUST_NEW=1`   | trust an unknown host key on first use, it is added to `~/.ssh/known_hosts` |

An application asks the user instead by installing callbacks, called from the thread connecting:

```python
from pynbus2.transport import ssh

ssh.set_prompts(
    password=lambda prompt: ...,                     # the password, None cancels connecting
    host_key=lambda host, key_type, fingerprint: ...,  # True trusts the unknown host key
)
```

```
ssh://[<user>@]<host>[:<port>][/<remote URI after its scheme://>][?identity=...&command=...&...]
```

The path without its leading `/` is the remote URI after its `<scheme>://`. A remote URI with an empty host
thus starts with `//`:

| ssh URI | proxy | remote URI |
|---------|-------|------------|
| `ssh://10.0.0.1:23//00000010/1` | `-t udp6` | `udp6:///00000010/1` |
| `ssh://nbus@10.0.0.1:23/nwdaq-main-hh1/conf` | `-t rible` | `rible://nwdaq-main-hh1/conf` |
| `ssh://daq//dev/ttyUSB0/00010002/3?baudrate=1000000` | `-t dgstream+serial` | `dgstream+serial:///dev/ttyUSB0/00010002/3?baudrate=1000000` |

| query      | meaning | default |
|------------|---------|---------|
| `identity` | private key file | from the SSH config |
| `command`  | remote command, only when the proxy is not the login shell of the remote user | none |
| others     | passed to the remote URI | |

The connection holds the remote transport connection the remote URI names, each socket is a proxy session
to a destination on it and all of them share one SSH connection. It behaves as the remote URI connected
directly: `nbus.socket()` connects to the destination the remote URI names (or the default of the remote
transport, e.g. the root interface of a `rible` device), `nbus.socket(<sid>, <ep>)` to a service ID or an
interface name, and `nbus.walk()` discovers the interfaces:

```python
with pynbus2.connect('ssh://daq') as nbus:      # a proxy started with -t udp6
    a = nbus.socket('00000010', 1)       # udp6:///00000010/1
    b = nbus.socket('00000011', 1)       # udp6:///00000011/1

with pynbus2.connect('ssh://nbus@daq/nwdaq-main-hh1') as nbus:  # -t rible
    print(nbus.walk())                   # the interfaces the device exports
    conf = nbus.socket('conf', 0)        # rible://nwdaq-main-hh1/conf
```

## Discovery

`pynbus2.probe(uri)` discovers connection URIs a step at a time. It takes any prefix of a URI and
returns a list of more complete ones, using the discovery method of the transport the scheme selects.
Probe again with one of the results to refine it further, or pass a complete one to `connect()`. A
URI naming a service may carry its protocol name and version as `p`/`pv` query parameters, a BLE device
URI the device name as `name`, for display only.

```python
pynbus2.probe('')                           # ['dgble://', 'dgstream+serial://', 'dgtext+serial://', ...]
pynbus2.probe('dgtext+serial://')           # ['dgtext+serial:///dev/ttyUSB0', ...]
pynbus2.probe('dgtext+serial:///dev/ttyUSB0')
# ['dgtext+serial:///dev/ttyUSB0/00010002/1?p=flash&pv=1.0.0', ...]
```

| transport         | scheme only         | with a device |
|-------------------|---------------------|---------------|
| `dgtext+serial`, `dgstream+serial` | serial ports | services heard advertising within 5 s |
| `dgble`           | bonded BLE devices  | the proto-dgble service endpoints |
| `rible`           | bonded BLE devices  | the exported interfaces |
| `swd`             | debug probes        | the virtual interfaces |
| `ssh`             | hosts in `~/.ssh/config` | whatever the proxied transport probes (e.g. the BLE devices for `rible`) |
| `udp6`            | not implemented     | not implemented |

Probing can block for seconds, run it off a UI thread.

## Message queue client

`pynbus2.mq.MqPollClient` polls the message queue of a device through the firmware `nbus-mq-poll`
bridge, exported as an interface speaking the `mq` protocol. Each poll acknowledges the batches
received so far and returns the new values; a reply with `pending` batches left is best followed by
another poll right away. The optional MQTT-style topic filter (`#`, `+`) replaces the remote
subscription.

```python
from pynbus2.mq import MqPollClient

with pynbus2.connect('rible://AA:BB:CC:DD:EE:FF') as nbus:
    client = MqPollClient(nbus.socket('mq', 0), topic='sensor/#')
    reply = client.poll()                   # None when no reply arrives
    for v in reply.values:
        print(v.host, v.ts, v.topic, v.value)
```

## SSH proxy

When the plumCore device is attached to a Linux DAQ controller, `pynbus2-ssh-proxy` makes the controller's
nbus2 network (e.g. its `udp6://` bridge or the BLE devices it is bonded with) reachable over SSH. It is
installed as the login shell of a dedicated user, sshd starts it once the connection is authenticated and it
speaks a CBOR protocol on its stdin/stdout, bridging it to the single pynbus2 transport selected by
`-t`/`--transport`. Needs the `proxy` extra and the extra of the proxied transport.

sshd runs the login shell without arguments, a small wrapper script selects the transport:

```sh
python3 -m venv /opt/pynbus2
/opt/pynbus2/bin/pip install 'pynbus2[proxy,ble]'
printf '#!/bin/sh\nexec /opt/pynbus2/bin/pynbus2-ssh-proxy -t rible "$@"\n' > /usr/local/bin/nbus-rible-proxy
chmod 755 /usr/local/bin/nbus-rible-proxy
echo /usr/local/bin/nbus-rible-proxy >> /etc/shells
useradd -m -s /usr/local/bin/nbus-rible-proxy nbus
# add the client public key to ~nbus/.ssh/authorized_keys
```

Both directions carry a sequence of CBOR maps (RFC 8742) without any further framing. The proxy starts with
a greeting wrapped in the CBOR self-describe tag, so its first bytes are always `d9 d9 f7` and a client can
skip anything sshd printed before it (motd, banners):

```
55799({"c": "hello", "p": "nbus2-ssh-proxy", "pv": "1.1.0", "t": <transport scheme>})
```

Requests carry the command in `c` and an optional request ID in `id`. Each request is answered in order with
one map echoing `id` and carrying either the result or `err`:

| request                                              | response                                   |
|------------------------------------------------------|--------------------------------------------|
| `{"c": "probe", "u": <prefix>}`                      | `{"u": [<uri>, ...]}`                      |
| `{"c": "connect", "u": <uri>}`                       | `{"t": <connection>, "d": [<sid>, <ep>] or null}` |
| `{"c": "disconnect", "t": <connection>}`             | `{}`                                       |
| `{"c": "walk", "t": <connection>}`                   | `{"i": [[<name>, <desc>], ...]}`           |
| `{"c": "open", "u": <uri>}`                          | `{"s": <session>}`                         |
| `{"c": "open", "t": <connection>, "d": [<sid>, <ep>]}` | `{"s": <session>}`                       |
| `{"c": "close", "s": <session>}`                     | `{}`                                       |
| `{"c": "send", "s": <session>, "d": <bytes>}`        | `{}`                                       |

URIs are those of the proxied transport without their `<scheme>://` prefix, e.g. `nwdaq-main-hh1/conf` for
`rible://nwdaq-main-hh1/conf` or `/00000010/1` for `udp6:///00000010/1`. `probe` works exactly as
`pynbus2.probe()`, an empty prefix probes the bare scheme (e.g. lists the bonded devices for `rible`).
`connect` opens the transport connection a URI names and holds it until `disconnect`, returning its number and
the destination the URI names, or the default one of the transport (e.g. the root interface of a `rible`
device), `null` if there is none. `walk` discovers the interfaces of a connection as `Nbus.walk()` does.
`open` opens a socket either to the destination a complete URI names, or to the destination `d` (a service ID
or an interface name, and an endpoint) on a connection.
Any number of sessions may be open at once, sessions to URIs differing only in their destinations (e.g.
`udp6:///00000010/1` and `udp6:///00000011/1`, or two services behind one serial port) share one transport
connection with each other and with `connect`, it is closed with the last of them. Received datagrams and failed sessions are reported by events
carrying `c` and no `id`:

```
{"c": "recv", "s": <session>, "d": <bytes>}
{"c": "closed", "s": <session>, "err": <message>}
```

Closing stdin (ending the SSH session) closes all sessions. The proxy logs to stderr, `--log-level DEBUG`
is useful when running it by hand.

## Extending

New transports subclass `pynbus2.transport.base.Transport` / `SocketBackend` and register for a
scheme via `pynbus2.register(scheme, TransportClass)`. A transport using the URI path for anything else than
a `/<sid>/<ep>` destination overrides the `split_uri()` classmethod, so connections can be shared. A transport may implement the `probe()`
classmethod to take part in discovery. A direct serial transport speaking the legacy nbus2 wire cipher
(ChaCha20-128 + halfsiphash) is not implemented.
