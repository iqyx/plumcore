# Status of the project

Current **development version is 0.5.0** (develop branch).

Current **stable version is 0.4.0** (2017-05-31).

History of older versions (the uMeshFw project) is truncated.


# Features and bug fixes to be included in the current development version

- Remove all old HAL interfaces and modules and migrate them to a new system
- Introduce a more general IMetric interface describing a group of metrics
  for a device/inteerface/service, etc. IMetric group will be described by a
  string and each metric inside the group will have it's own name (string),
  value and unit. Several types of values will be supported (integer number,
  float number, string).
- DataProcess service for data processing using flow graphs
- Refactoring of the CLI service to be more concise and readable. Splitting
  CLI service's code into multiple submodules.
- DataProcess tcp-client node
- DataProcess text-serialize node
- CAN interface
- UXB CAN driver
- configuration exporting, saving and loading
- fix of the /system/bootloader menu


# Features and bug fixes to be included in future versions

- I2C bus and I2C device drivers
- IFlash interface
- Virtual flash volumes. Each volume will be defined by index of a starting
  erase sector/block and length in erase sectors/blocks. Optionally a volume
  table header may be supported which will describe the layout of flash volumes
  for the entire chip.
- a system wide logging service similar to a syslog daemon to receive messages
  from other modules and route them to configurable destinations
- unify licensing

