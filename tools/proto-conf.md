# proto-conf.py

Command-line tool to **walk, read and write** the configuration tree of a plumCore device over
nbus2. It talks to the device's `conf` interface (the same tree services expose via
`*_get_conf()`), so any service that publishes a configuration subtree — for example
[`mq-compensation`](../services/mq-compensation/README.md) — can be inspected and configured with
it.

## Connecting to a device

The tool does not speak to hardware directly: it goes through the
[`pynbus2`](pynbus2/) library, which is bundled in `tools/pynbus2/` and imported straight from the
source tree (no installation needed). **The device must therefore be reachable by one of the
connection methods that `pynbus2` supports**, selected by the scheme of the connection URI you pass
as the first argument.

The generic URI shape is:

```
<scheme>://<host>[:<port>][/<sid>[/<ep>]][?<key>=<value>&...]
```

The path may carry a **default destination** as `/<sid>` or `/<sid>/<ep>` (service ID and
endpoint). proto-conf requires a destination in the URI — without one it exits with an error,
because it has nowhere to send the requests.

### Supported transports

| Scheme           | Method                                                        |
|------------------|---------------------------------------------------------------|
| `udp6`           | nbus2 over a host-side UDP/IPv6 bridge (the usual method)      |
| `dgtext+serial`  | nbus2 directly over a serial port, dgtext-framed               |

**UDP/IPv6 bridge (`udp6`)** — a host-side bridge exposes the nbus2 network on an IPv6 prefix where
the lower 32 bits of the address are the service ID and the UDP port is `base_port + ep`. The bridge
handles the nbus2 framing and crypto.

```
udp6:///00010002/3                       default bridge, destination service 00010002 endpoint 3
udp6://[fd00:dead:beef::1]:52000/00010002/3   explicit bind address and base port
udp6:///00010002/3?prefix=fd00:dead:beef::&self=1   override the network prefix and our own sid
```

**Serial (`dgtext+serial`)** — nbus2 packets over a serial port, framed as dgtext text lines. The
device path occupies the URI host/path, so the default destination is the last two path components.
The host performs the nbus2 BLAKE2s-SIV packet protection itself, so the `key` query parameter is
the preshared key (default `abcd`, matching the firmware).

```
dgtext+serial:///dev/ttyUSB0/00010002/3?baudrate=1000000
dgtext+serial:///dev/ttyUSB0/00010002/3?key=abcd
dgtext+serial://COM3/00010002/3
```

If `pynbus2` reports an unsupported scheme it lists the ones it knows; new connection methods are
added by registering a transport in `pynbus2`.

## Usage

```
proto-conf.py <uri> [--walk [PATH]] [--read PATH] [--write PATH VALUE] [--verbose]
```

| Option              | Purpose                                                                  |
|---------------------|--------------------------------------------------------------------------|
| `uri`               | nbus2 connection URI carrying the destination (see above)               |
| `-w`, `--walk [PATH]` | walk and pretty-print the tree at `PATH` (slash separated; default root) |
| `-r`, `--read PATH` | read a single node and print its raw value (script-friendly)            |
| `-W`, `--write PATH VALUE` | write `VALUE` to the node at `PATH`                               |
| `-v`, `--verbose`   | log every nbus2 request/response to stderr                              |

Paths are slash separated and address nodes inside the configuration tree, e.g.
`compensation/press/c1`. The actions may be combined in one invocation; they run in the order
walk → read → write.

## Walking the tree

`--walk` (optionally given a starting `PATH`) prints the tree as an indented hierarchy. Each node
shows its name, type and — for value nodes — its current value:

```
proto-conf.py udp6:///00010002/3 --walk
proto-conf.py udp6:///00010002/3 --walk compensation/press
```

```
root
└── compensation (subtree)
    └── press (subtree)
        ├── x_ref (f) = 24.5
        ├── c0 (f) = 0.0
        ├── c1 (f) = 1.0
        ├── t_ref (f) = 25.0
        ├── tc1 (f) = 0.0
        └── tc2 (f) = 0.0
```

The responder caps each walk to a maximum number of siblings, so the tool walks repeatedly from the
last returned node to gather every child; this is transparent to the user.

## Reading a value

`--read` prints the raw value of a single node and nothing else, which is convenient in scripts.
Byte-string (`bstr`) values are printed as hex. The tool exits non-zero if the node cannot be read.

```
proto-conf.py udp6:///00010002/3 --read compensation/press/c1
```

## Writing a value

`--write PATH VALUE` first `stat`s the node to learn its type, parses `VALUE` accordingly, and sends
the write. Subtree and `none` nodes are rejected as not writable. The value is parsed according to
the node type:

| Type                          | Accepted input                                                    |
|-------------------------------|-------------------------------------------------------------------|
| `f`                           | floating point, e.g. `24.5`                                       |
| `u32`/`u16`/`u8`/`enum`       | integer, `0x`/`0o`/`0b` prefixes accepted (base 0)               |
| `s32`/`s16`/`s8`              | signed integer, prefixes accepted                                |
| `bool`                        | `true`/`1`/`yes`/`on` or `false`/`0`/`no`/`off`                  |
| `str`                         | text, taken verbatim                                             |
| `bstr`                        | hex string, e.g. `deadbeef`                                      |

```
proto-conf.py udp6:///00010002/3 --write compensation/press/c1 1.0023
proto-conf.py udp6:///00010002/3 --write compensation/press/tc1 -0.00042
```

On success it prints `ok`; on a parse error or a rejected write it prints the reason to stderr and
exits non-zero.

## Debugging

Add `--verbose` to log every nbus2 CBOR request and its response (or a timeout) to stderr. This is
the quickest way to see whether the device is reachable and which node a request targeted.

## See also

- [`mq-comp-make-coeffs.py`](mq-comp-make-coeffs.md) — computes compensation coefficients that you
  then write into the tree with this tool.
- [`mq-compensation` service](../services/mq-compensation/README.md) — the configuration layout of
  one such tree.
- `proto-conf-gui.py` — a graphical front end over the same configuration tree.
