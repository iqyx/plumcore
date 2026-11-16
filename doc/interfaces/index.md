# Interfaces

An interface is a contract between a service implementing some functionality (a driver, a protocol handler, a
filesystem) and its users. plumCore interfaces are plain C: a table of function pointers (a virtual method table,
*vmt*) and an instance structure holding a pointer to that table and to the implementer's context. A user calls
a method through the instance without knowing which service implements it, which lets services be combined freely
in a port or an application and discovered at runtime through the service locator.

All interfaces are declared in `services/interfaces/` and included as `<interfaces/name.h>`. They are header only,
contain no code and emit no data.

The design rules shared by all interfaces are described in [Interface concepts](concepts.md). Interfaces with
nontrivial semantics have their own page:

- [Power source/sink device](power.md)
- [Remote interface](remote-interface.md)


## Interface generations

The interfaces evolved over time and three generations coexist in the tree. New interfaces are written in the
modern style, older ones are migrated when they are reworked.

| Generation | Interfaces | Characteristics |
|------------|------------|-----------------|
| Modern (reference) | `block`, `remote-interface`, `notify`, `job` | `const struct x_vmt`, `X *self`, documented methods and return codes, threading and state rules stated |
| Modern, less documented | `ble`, `pm`, `fs`, `conf`, `clock`, `ow`, `led`, `event`, `stream`, `flash`, `mux`, `can`, `beeper` | vmt layout is modern, per-method documentation is partial or missing |
| Modern layout, undocumented | `adc`, `dac`, `sensor`, `datagram`, `gpio`, `pwm`, `spi`, `uart`, `i2c-bus`, `fb`, `painter`, `window`, `power`, `waveform-source`, `waveform-sink` | vmt layout is modern, methods carry no documentation |
| Legacy | `servicelocator`, `radio`, `cellular`, `log`, `worn-log`, `lora`, `uxbbus`, `uxbdevice`, `uxbslot`, `measurement`, `clock/descriptor`, `radio-mac/*`, `plog/*` | `Interface` base from `interface.h`, `void *parent` as the first method argument, init/free helpers in a `.c` file |

`applet.h` is a descriptor rather than a vmt interface (a static structure describing an applet).
