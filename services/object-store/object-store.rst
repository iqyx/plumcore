===================================
ELF object store service
===================================

Generic filesystem API
====================================

- open file for reading: linear search for ELF with the specified name (use ELF comments + version)
- open for writing: do not consider the supplied file name, lock the object store for writing,
  find the first empty slot, keep it locked until the file is closed
- close file: unlock any locks
- read: provide random access, practically
- write: use write buffer of suitable size, program the flash when full or when the file is closed


ELF access API
=================================



to-do:

- how to check if the current slot is big enough
- how to tell open('w') to use another slot
