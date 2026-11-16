# Interface concepts

This page describes how modern plumCore interfaces are designed, implemented and used. It is the reference for
writing new interfaces and for migrating legacy ones. `block.h`, `remote-interface.h`, `notify.h` and `job.h` are
the reference headers, see the [interface generations](index.md#interface-generations) table for the others.


## Anatomy of an interface header

An interface header contains, in this order:

1. The license header: SPDX identifier (`GPL-3.0-or-later`), one-line description, copyright, "All rights reserved."
2. A conceptual comment (what the interface abstracts, who implements it, who uses it). Longer explanations go to
   `doc/interfaces/<name>.md` and the header refers to it.
3. `#pragma once` and includes. Only standard headers and other interfaces (`<interfaces/notify.h>`), never
   a service header, `main.h` or FreeRTOS.
4. The return code enum.
5. Data types used by the methods (plain `struct`/`enum`).
6. A forward typedef of the handle type.
7. The vmt structure, each method documented.
8. The instance structure.

A minimal complete interface:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Generic foo device interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * A foo device measures foos. All methods are blocking. Implementations are responsible for locking, the interface
 * may be used from multiple tasks concurrently.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>


typedef enum {
	FOO_RET_OK = 0,
	FOO_RET_FAILED,
	FOO_RET_BAD_ARG,
	FOO_RET_TIMEOUT,
} foo_ret_t;

struct foo_status {
	/** The device is powered and calibrated. */
	bool ready;
	/** Number of foos measured since start. */
	uint32_t count;
};

typedef struct foo Foo;

struct foo_vmt {
	/**
	 * @brief Measure a single foo
	 *
	 * @param self Foo interface instance
	 * @param value Measured value in SI base units
	 * @param timeout_ms Time to wait for the measurement, 0 to not wait at all
	 * @return FOO_RET_TIMEOUT if no measurement completed within @p timeout_ms, FOO_RET_FAILED on error or
	 *         FOO_RET_OK otherwise.
	 */
	foo_ret_t (*measure)(Foo *self, float *value, uint32_t timeout_ms);

	/**
	 * @brief Get the device status
	 *
	 * Non-blocking. NULL if the device does not report its status.
	 *
	 * @param self Foo interface instance
	 * @param status Current status
	 * @return FOO_RET_FAILED if the status is not known or FOO_RET_OK otherwise.
	 */
	foo_ret_t (*get_status)(Foo *self, struct foo_status *status);
};

struct foo {
	const struct foo_vmt *vmt;
	void *parent;
};
```


## Naming

| Item | Form | Example |
|------|------|---------|
| Header | `services/interfaces/<name>.h`, kebab-case | `remote-interface.h` |
| Handle type | CamelCase typedef of the instance struct | `typedef struct remote_interface RemoteInterface;` |
| Instance struct | `struct <name>` | `struct remote_interface` |
| vmt struct | `struct <name>_vmt`, not typedef'd | `struct remote_interface_vmt` |
| Return type | `<name>_ret_t` typedef of an enum | `remote_interface_ret_t` |
| Return codes | `<NAME>_RET_*` | `REMOTE_INTERFACE_RET_CLOSED` |
| Other types | plain `struct <name>_*` / `enum <name>_*`, no typedef | `struct block_status`, `enum job_state` |
| Methods | short verbs, no interface prefix | `read`, `get_geometry`, not `block_read` |

Only the handle type (and the return type) is typedef'd. Structures and enums whose members are accessed by the user
stay plain `struct`/`enum`. Object tree members use a common prefix (`Ble`, `BleSrv`, `BleChar`).


## Instance structure

```c
struct remote_interface {
	const struct remote_interface_vmt *vmt;
	void *parent;

	/** Description of the interface, owned by the implementation and static for its whole lifetime. */
	const struct remote_interface_desc *desc;
};
```

- `vmt` is always the first member and always points to `const` (the table lives in flash).
- `parent` is the implementer's context, opaque to the user. In an object tree it is a typed pointer to the owner
  object instead (see [Object trees](#object-trees)).
- Additional members are allowed only for static, read-only data the user accesses directly, such as a description
  (`desc` above, `const struct sensor_info *info` in `Sensor`). They are `const` pointers owned by the implementation,
  there are no getter methods for them.


## Methods

- The first argument is always the instance as its handle type, `Foo *self`. Never `void *`.
- Methods return `<name>_ret_t`. Produced values (including other interface pointers) are returned through
  out-parameters, never as the return value.
- Input buffers are `const void *buf` + `size_t len`, output buffers are `void *buf` + `size_t size` + `size_t *len`
  (the actual length). Ownership is never transferred: the caller owns the buffer and may reuse it as soon as the
  method returns.
- A method that may wait takes `uint32_t timeout_ms` as its last argument. 0 means "do not wait at all", the method
  returns `*_RET_TIMEOUT` if it could not complete in time.
- Every method is documented with `@brief`, `@param` for every argument (including `self`) and `@return` listing
  each return code it can produce and when.
- Values are in SI base units (V, A, Ah, ratio instead of %).

Calling a method:

```c
size_t block_size;
uint32_t block_count;
if (sd->vmt->get_geometry(sd, &block_size, &block_count) != BLOCK_RET_OK) {
	return APP_RET_FAILED;
}
```


## Return codes

```c
typedef enum {
	/** The function performed as intended with no unexpected result. */
	BLE_RET_OK = 0,
	/** A generic failure not covered by a more specific code. */
	BLE_RET_FAILED,
	/** A NULL pointer was given as an instance or a required argument. */
	BLE_RET_NULL,
	/** A wrong argument value was used when calling the function. */
	BLE_RET_BAD_ARG,
	...
} ble_ret_t;
```

`*_RET_OK = 0` is first, `*_RET_FAILED` second. Further codes are added only when the user can act on them
differently. Commonly used codes:

| Code | Meaning |
|------|---------|
| `NULL` | a NULL instance or required argument |
| `BAD_ARG` | an argument value out of range (zero count, oversized message, unknown session) |
| `BAD_STATE` | arguments are valid but the object is in the wrong state |
| `TIMEOUT` | the operation did not complete within `timeout_ms` or the device does not respond |
| `NOT_IMPLEMENTED` | the method exists but the implementation does not support it |
| `NOMEM` | a preallocated resource (slot, table entry, buffer) is exhausted |
| `EOF` | end of data reached, no data transferred |
| domain specific | `BLOCK_RET_NO_MEDIA`, `BLOCK_RET_READ_ONLY`, `REMOTE_INTERFACE_RET_CLOSED`, ... |

Codes are positive and sequential. Each code is documented either in the enum or in the `@return` of every method.
An implementation maps its internal errors to the interface codes in a single helper (`sd_ret_to_block_ret()`).


## Optional methods

A method may be declared optional. Its vmt slot is then NULL when the implementation does not support it and the
documentation says so ("NULL if the device has no removable media"). Everything else is mandatory. The user checks
the slot before calling:

```c
struct block_status st = {0};
if (media->vmt->get_status != NULL && media->vmt->get_status(media, &st) == BLOCK_RET_OK && st.present) {
	...
}
```

Use NULL for a capability the implementation does not have at all, `*_RET_NOT_IMPLEMENTED` for a method which
exists in the ABI but is not implemented yet (eg. reserved client role methods of `Ble`).


## Contract statements

The interface documentation states explicitly:

- **Blocking**: whether methods block, which ones are non-blocking (eg. `get_status` for status displays).
- **Locking**: implementations are responsible for locking, the interface may be used from multiple tasks
  concurrently. The user never takes an external lock.
- **Context**: which methods may be called from callbacks or other task contexts, and which must not block.
- **State machine**: states, transitions and how they are observed. Examples:
    - `Job`: scheduled → running ⇄ suspended → scheduled, with the last run outcome kept as a separate result.
    - `Block` removable media: after a media change all methods return `BLOCK_RET_DEVICE_CHANGED` until
      `get_geometry` acknowledges it.
- **Delivery guarantees** for data paths (whole or nothing, may be lost/reordered).
- **Lifetime**: what is static (`desc`), what is valid only during a call (event payloads).


## Header only

Interfaces contain no code and emit no data: no `<name>.c`, no `init`/`free` helpers, no wrapper functions, no
global variables. The implementer fills `vmt` and `parent` directly. Lookup tables are provided as initializer macros
the user materializes in its own storage:

```c
static const struct pm_state_name pm_state_names[] = PM_STATE_NAMES;
```


## Implementing an interface

The interface instance is embedded in the service structure, the vmt is a `static const` table in the service
source file, `init()` wires both:

```c
/* spi-sd.h */
typedef struct {
	...
	SemaphoreHandle_t lock;

	/* Block device interface instance */
	Block block;
} SpiSd;
```

```c
/* spi-sd.c */
static const struct block_vmt block_vmt;

spi_sd_ret_t spi_sd_init(SpiSd *self, struct interface_spidev *spidev) {
	...
	self->block.vmt = &block_vmt;
	self->block.parent = self;
	...
}

static block_ret_t spi_sd_read(Block *self, uint32_t lba, void *buf, size_t count) {
	SpiSd *sd = (SpiSd *)self->parent;

	if (buf == NULL || count == 0 || lba >= sd->size_blocks || count > sd->size_blocks - lba) {
		return BLOCK_RET_BAD_ARG;
	}

	xSemaphoreTake(sd->lock, portMAX_DELAY);
	spi_sd_ret_t ret = spi_sd_read_data(sd, lba, buf, count * 512, 1000);
	xSemaphoreGive(sd->lock);

	return sd_ret_to_block_ret(ret);
}

static const struct block_vmt block_vmt = {
	.get_geometry = spi_sd_get_geometry,
	.read = spi_sd_read,
	.write = spi_sd_write,
	.flush = spi_sd_flush,
	/* .get_status left NULL, optional */
};
```

The user accesses the interface as a member (`&sdmmc1.block`), there is no getter function. A service exposing
several interfaces of the same kind groups them in a named anonymous struct:

```c
struct conf_cbor {
	...
	struct {
		struct conf_cbor_job save;
		struct conf_cbor_job load;
	} jobs;
};
/* used as &conf_cbor.jobs.save.job */
```

A service consuming interfaces receives them as plain interface pointers in its configuration structure (`Flash *`,
`Block *`, `Notify *`). Implementation-private handler tables are never part of the configuration or the API, they
are selected internally in `start()`.


## Notifications

When a producer needs to wake up a consumer, the consumer implements the `Notify` interface and passes it to the
producer (eg. in `RemoteInterface.open`). A notification is a hint carrying a producer-defined `uint32_t` value
(messages waiting, bytes free), the consumer queries the actual state afterwards. `notify()` may be called with the
producer's locks held, so it must not block and must not call back into the producer:

```c
static notify_ret_t notify_notify(Notify *notify, uint32_t value) {
	(void)value;
	RemoteInterfaceBle *self = notify->parent;
	xTaskNotify(self->task, REMOTE_INTERFACE_BLE_EV_TX, eSetBits);
	return NOTIFY_RET_OK;
}

static const struct notify_vmt remote_interface_ble_notify_vmt = {
	.notify = notify_notify,
};
```

Prefer `Notify` over a raw `void (*cb)(void *ctx, ...)` callback. A raw callback carrying data (`ble_event_cb`)
is acceptable only for asynchronous events which cannot be polled, it has a single owner, its payload is valid only
during the call and it must not block. Consumers other than the owner poll a status snapshot instead
(`Ble.get_status`).


## Sessions

An interface serving several independent users at once (remote peers, transports) keeps per-user state in sessions.
The implementation owns a fixed number of preallocated session slots (advertised in its description, eg.
`desc->max_sessions`), `open()` returns an opaque `void *session` pointing to one of them and `close()` frees it.
Every data method takes the session as its second argument. No session state is allocated by the user.


## Object trees

A device with a hierarchical structure is modelled as a tree of interface objects (`Ble` → `BleSrv` → `BleChar`).

- Child objects are preallocated by the caller and filled in by the driver (`ble->vmt->add_service(ble, &uuid, &srv)`).
- Each object has its own vmt and a typed `parent` pointer to its owner. Driver private per-object data is in `priv`.
- A method dispatches on the object it acts upon (characteristic operations take a `BleChar *`). Walking the parent
  chain leads to the root object whose `parent` holds the driver context.

```c
struct ble_char {
	const struct ble_char_vmt *vmt;

	/** Owning service. */
	BleSrv *parent;

	/** Driver private per characteristic handle. */
	void *priv;
};
```


## Static descriptions

Properties fixed for the whole lifetime of an instance (protocol name, MTU, unit, maximum sessions) are provided as
a `const struct <name>_desc *` member of the instance rather than as getter methods. The structure is owned by the
implementation, usually a `static const` in flash. Properties which may change at runtime (media geometry, link
state) are read with a method instead.


## Discovery

Interfaces are advertised and discovered through the service locator. Each interface has an
`ISERVICELOCATOR_TYPE_*` value in `services/interfaces/servicelocator.h` (hand-edited, new values are appended at
the end of the enum). The locator still uses the legacy `Interface *` type, hence the casts:

```c
/* port.c, the implementer */
iservicelocator_add(locator, ISERVICELOCATOR_TYPE_BLOCK, (Interface *)&sdmmc1.block, "sd");

/* application, the user */
Block *sd = NULL;
if (iservicelocator_query_name_type(locator, "sd", ISERVICELOCATOR_TYPE_BLOCK, (Interface **)&sd) !=
    ISERVICELOCATOR_RET_OK) {
	return APP_RET_FAILED;
}

/* a transport enumerating all instances of a type */
Interface *iface = NULL;
for (size_t i = 0; iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_REMOTE_INTERFACE, i, &iface) ==
     ISERVICELOCATOR_RET_OK; i++) {
	RemoteInterface *ri = (RemoteInterface *)iface;
	...
}
```

Short-lived objects (jobs) are managed by a dedicated manager service instead of the locator.


## Migrating a legacy interface

| Legacy | Modern |
|--------|--------|
| `Interface interface` base member, `#include "interface.h"` | removed, `vmt` is the first member |
| `typedef struct { ... } foo_vmt_t;`, `foo_vmt_t *vmt` | `struct foo_vmt { ... };`, `const struct foo_vmt *vmt` |
| methods take `void *parent` | methods take `Foo *self`, the implementation reads `self->parent` |
| `foo_init()`/`foo_free()` and wrappers in `foo.c` | no `.c` file, the implementer assigns `vmt`/`parent` |
| typedef'd data structs (`fs_stat_t`, `File`) | plain `struct`, typedef only the handle |
| negative return codes, `*_RET_NULL` for everything | positive codes, specific codes the user can act on |
| getter methods for static properties | `const` description member |
| undocumented methods | documented methods, return codes and contract |
| `BSD-2-Clause` with boilerplate | `GPL-3.0-or-later` SPDX header |

The migration changes all implementers and users in the same commit (`refactor(srv/interfaces): rework the Block
interface` reworked `block.h`, `spi-sd` and `worn-log-block` together).
