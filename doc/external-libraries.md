# External libraries

External libraries live in the `lib` directory, one subdirectory per library.
The framework wraps them in thin wrappers exposing a common interface (see
[Concepts](concepts.md)), but the library itself, its sources, its patches and
its build rules stay under `lib/<name>/`.

A library is either **fetched from the internet** at build time or **vendored
in-tree**. Fetched sources are cloned into `lib/<name>/src/`, which is
git-ignored (see `.gitignore`), so upstream code is never committed into this
repository. In-tree libraries keep their sources under `lib/<name>/` and are
committed normally.

## Pulling sources from the internet

Fetching is driven from the library's `SConscript` using helper methods added
to the SCons environment (defined in `lib.SConscript` and `sbom.SConscript` at
the repository root). The main one is `env.Git()`:

```python
env.Git('https://github.com/wren-lang/wren/', branch='main')
```

`env.Git(url, dir='src/', branch=None)` does a shallow (`depth=1`) clone of
`url` into `dir` (default `src/`). `branch` pins the checkout to a branch or
tag; pinning to a released tag is preferred over tracking a moving branch:

```python
env.Git('https://github.com/FreeRTOS/FreeRTOS-Kernel', branch='V11.1.0')
```

The clone runs once. A `.downloaded.stamp` file marks it done, so later builds
skip re-cloning. Deleting `lib/<name>/src/` and the stamp forces a fresh pull.

There is currently no tarball-download helper — libraries that are not fetched
over git are committed to the tree directly (for example `lib/fec-golay/`,
`lib/plumcore-cryptolib/`). If you need a source that is only distributed as a
tarball, vendor it in-tree rather than fetching it at build time.

### Patching

Upstream sources often need small local changes (to fit a small MCU, to fix a
build issue, to make a compile-time limit configurable). Keep those as patch
files next to the `SConscript` and apply them right after cloning with
`env.Patch()`:

```python
env.Patch([
    '0001-configurable-compiler-limits.patch',
    '0002-configurable-minimal-core-library.patch',
])
```

Patches are applied once and marked with `.patched.stamp`. Name them with a
numeric prefix so they apply in order, and keep the reason for each patch in a
comment in the `SConscript`.

### Building

Header-only or source-drop-in libraries just need their `.c` files compiled and
their include directories added to `CPPPATH`. Libraries with their own build
system can be driven through `env.Make(target, cwd=...)`, which runs `make`
once (marked with `.compiled.stamp`); `lib/libopencm3/` is an example.

## Adding a new library

1. Create `lib/<name>/`.
2. Add `lib/<name>/SConscript` (see the example below). It receives `env`,
   `conf` and `objs`, guards its work on its Kconfig symbol, fetches/patches the
   sources, and appends its objects and include paths.
3. Add a Kconfig entry (see below).
4. For an in-tree library, add a `lib/<name>/.gitignore` if part of the tree
   should stay ignored while committing the rest (the top-level `.gitignore`
   ignores `lib/*/src`).

A minimal `lib/<name>/SConscript`:

```python
Import("env")
Import("conf")
Import("objs")

if conf["LIB_FOO"] == "y":
    env.Git('https://github.com/example/foo', branch='v1.2.3')

    objs.append(env.Object([File(Glob('src/*.c'))]))
    env.Append(CPPPATH = [Dir('src/include')])
```

The `lib/SConscript` walks `lib/` and sources every `lib/<name>/SConscript`
automatically, so no central list has to be edited.

## Configuration

Compile-time configuration uses Kconfig. Each library declares its symbols in
`lib/Kconfig`, which the top-level `Kconfig` sources under the "Libraries" menu.
Because `lib/Kconfig` is a single file sourced automatically, a new library only
needs its symbols added there — nothing else has to be wired up.

Every library gets a boolean switch that enables it, and any further options
depend on it:

```kconfig
config LIB_WREN
    bool "wren.io language compiler & VM"
    default n

if LIB_WREN

config LIB_WREN_MINIMAL_CORE
    bool "Strip rarely-used methods from the Wren core library"
    default n
    help
      ...

endif
```

Naming rules:

- The enable switch is `LIB_<NAME>` (upper case, e.g. `LIB_WREN`,
  `LIB_TINYCBOR`, `LIB_LIBOPENCM3`).
- Sub-options are prefixed with the same `LIB_<NAME>_` (e.g.
  `LIB_WREN_MINIMAL_CORE`, `LIB_CMSIS_DSP`).

The generated configuration is pulled into the build automatically: scons reads
the resolved `.config` and exposes it to every `SConscript` as the `conf`
dictionary. A library guards its work on its own symbol, for example
`if conf["LIB_WREN"] == "y":`, and reads its sub-options the same way. The same
values are available to C code as `CONFIG_LIB_*` defines. Concrete per-port
values belong in the port's `config/<port>_defconfig`, not in Kconfig defaults.

## SBOM

`scons sbom` generates an SPDX 2.3 SBOM (for EU CRA compliance) describing every
library the currently configured firmware compiles in. Collection is automatic:
`env.Git()` is wrapped so each clone registers itself as an external component,
and in-tree first-party components register with `env.Component()`. Because both
only fire inside the enabled `if conf[...] == "y":` guards, the SBOM describes
exactly what was built. See `tools/sbom.py` for the generator.

For each library the generator resolves, from the working tree, without any
hand-maintained metadata:

- **Version / commit** — for fetched libraries, from the git metadata of the
  clone (the pinned tag/branch and commit); for in-tree components, from the
  last commit touching their tracked files.
- **License** — from the library's `LICENSE`/`COPYING`/`COPYRIGHT` file (matched
  against known license texts) or, for first-party sources, from the
  `SPDX-License-Identifier:` header tags.
- **Source location** — the upstream clone URL for fetched libraries.

Anything that cannot be detected is emitted as `NOASSERTION` rather than
guessed, so keep an upstream `LICENSE` file in the clone and an accurate
`branch=` tag on `env.Git()` to get complete SBOM entries.
