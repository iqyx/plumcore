# EP-c55: Configuration tree

![image](https://img.shields.io/badge/Status-Draft-blue)

![image](https://img.shields.io/badge/plumCore-0.8.0--dev-gray?labelColor=purple)

## Introduction

At the beginning of te plumCore project there was only a single source of configuration values in the system - a CLI
configuration. The CLI was structured as a tree of configuration values which can be read and written. Additionally,
configuration values could be executed calling a specificied callback.

As the framework became bigger and more complex it started to be clear it is conceptually wrong:

- CLI is not the sole source of configuration anymore. There can be a whole new and different style of a configuration
  interface in the system, eg. a graphical user interface. In the current design it is not possible to provide one
  without duplicating nearly all the configuration code.
- Service code cannot be decoupled from the CLI code. Even worse, the service cannot be configured when the CLI is
  disabled. On the other hand, if the CLI contains a module to configure a service and that module is not properly
  conditionally compiled, the service becomes a dependency for te CLI.
- Routines in the CLI managing the configuration use various ways to access the console. Reading and writing is not
  standard enough despite being done over a `Stream` interface.
- CLI configuration code contains a lot of formatting, table building, etc.

### Key-word usage

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT", "SHOULD", "SHOULD NOT", "RECOMMENDED", "NOT
RECOMMENDED", "MAY", and "OPTIONAL" in this document are to be interpreted as described in BCP 14 \[RFC2119\]
\[RFC8174\] when, and only when, they appear in all capitals, as shown here.

### License

This work is licensed under CC BY-SA 4.0. To view a copy of this license, visit
<https://creativecommons.org/licenses/by-sa/4.0/>

© 2025 Marek Koza \<<qyx@krtko.org>\>

## The proposed changes

An extremely simple configuration interface named `Conf` is proposed. Its purpose would be to allow services to define a
configuration tree without any additional burden. The interface can be used by:

- the CLI service to export the configuration tree as a tree-structured command line interface allowing the user to
  traverse the tree and configure individual configuration values.
- a configuration loader to load the tree structured values from eg. a file
- a LCD user interface to display the configuration tree appropriately, to allow the user to traverse the tree using
  buttons and/or to change individual values with a keyboard or a touch screen
- any other protocol drivers exporting the configuration to the world (eg. a HTTP REST API to configure the device)

On the `Conf` interface client side a configuration manipulation library can be used which would ease the access, namely
traversing the tree, searching for a specific path within the tree, filtering all children by a wildcard filter or a
regexp, etc.

On the *server* side (that is, in the service implementing the `Conf` interface) a configuration builder library can be
used to create the tree, append additional siblings or children, automatically mapping configuration values to C
variables, calling getter/setter callbacks, etc.

## Value flags

- `CONF_READ` - read only configuration value. Can be a `CONF_CONST`, `CONF_STATUS` value or any other value which once
  became read only.
- `CONF_WRITE` - value can be modified/written
- `CONF_CONST` - a constant value read from a nonvolatile memory or any other source which cannot be modified during
  runtime. It can be a dynamically detected configuration. It cannot change during runtime.
- `CONF_STATUS` - same as `CONF_CONST` but it can change (and usually changes often) during runtime, eg. interface
  packet counters, uptime counter, radio RSSI, etc.
- `CONF_DETAIL` - is an expert mode configuration variable displayed/settable only when the configuration interface is
  set to te expert mode, or eg. the CLI is set to print all details within a subtree

## Value types

- `u8` - unsigned integer, 8 bit wide
- `s8` - signed integer, 8 bit wide
- `u16`
- `s16`
- `u32`
- `s32`
- `b` - boolean
- `f` - float
- `str` - character string
- `bstr` - byte string
