===========================================
Ports
===========================================

A "port" is the board‑/hardware‑specific implementation layer that adapts the generic plumCore framework
to run on a particular microcontroller board or hardware platform.

It contains, among others:

- clock initialization happening in the early stage of the boot process
- configuration of GPIO ports
- instantiation of services which are run on the target hardware and injecting the required dependencies
- advertising interfaces of the instantiated services
- interrupt to service glue for interrupts
- hardware revision detecting logic for cases when a single firmware runs on hardware of
  multiple (semi-)compatible revisions
- linker script for the target MCU
- FreeRTOS configuration specific for the target MCU
- SConscript to build the port code
- documentation of the port in the doxygen form (for the code) and reST (well, for the rest and high-level documents)

.. toctree::

	nwdaq-br28-fdc/index.rst
	nwdaq-rtd18-fdc/index.rst
	nwdaq-if-lpcan/index.rst

