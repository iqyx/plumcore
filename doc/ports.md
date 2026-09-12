# Ports

A *port* is the board- and hardware-specific implementation layer that adapts
the generic plumCore framework to a particular microcontroller board. It covers,
among other things, the early clock bring-up, GPIO configuration, instantiation
of the services run on the target and the injection of their dependencies,
interrupt-to-service glue, hardware-revision detection, the linker script, the
FreeRTOS configuration for the target MCU and the board's build rules.

Because port documentation is live, closely tied to specific and evolving
hardware, it lives next to the code it describes rather than in this manual, and
it is **not rendered into the HTML or PDF documentation**. Each port keeps its
own `README.md` in its directory, which you can read online on GitHub:

- Repository: <https://github.com/iqyx/plumcore>
- Ports directory: <https://github.com/iqyx/plumcore/tree/develop/ports>

Open the subdirectory of the port you are interested in — for example
`ports/nwdaq-m-ff14/` — to see its hardware description and bring-up notes.
