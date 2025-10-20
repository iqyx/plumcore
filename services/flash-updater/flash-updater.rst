===================================
Flash updater service
===================================


Features
==================

Initialization:

- initialize with a target ``Flash`` volume/partition
- get the target ``Flash`` properties and create a strategy for updating
  depending on page size, erase size (eg. isn't it faster to compare first,
  flash only when a difference is found?)

Pre-check firmware:

- find the firmware requested by the configuration in the source object store
- double check the version and name
- check firmware integrity and signature, if requested

Backup:

- check if there is any firmware already flashed on the target ``Flash``
  (use eg. ``object-store`` service to determine the version and format based on metadata)
- allow backup of the original firmware (eg. to a file or a object on a ``Fs``)
- break the process if the old firmware doesn't match the flashing configuration
- break the process if backup was not successful despite being configured as mandatory

Flash:

- erase the target ``Flash`` volume, check if erased properly, check for any errors
- flash the selected firmware to the target ``Flash`` volume
- check the firmware integrity
