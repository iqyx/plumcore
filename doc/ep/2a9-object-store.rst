=========================
EP-2a9: ELF object store
=========================

.. image:: https://img.shields.io/badge/Status-Draft-blue

.. image:: https://img.shields.io/badge/plumCore-0.8.0--dev-gray?labelColor=purple



plumCore framework is continuously moving towards a dynamically loaded architecture where the whole system is split
into multiple loosely coupled components (services) which can be loaded during runtime on-demand. Every component is
contained in an ELF file together with all information required to load and run it. Because the requirements are
slightly different from what a common flash filesytem provides and the size of the implementation of such a filesystem
is also prohibitive, a simple *object store* is proposed.


Requirements
===================

XIP and mmap
----------------

XIP (execute in place) functionality is mandatory. *Object store* backing storage must be memory mapped in order to
support XIP at all. For the compiled firmware image or a single dynamically loadable component it means that the exact
location of the object within the mapped memory must be known. In the case of static linking (before the ELF file is
deployed to the device) the resulting linked ELF file needs to be saved at a specific location, or, in the case of
relocatable ELF object files, the location of the stored ELF object file needs to be determined to perform the
relocation. For dynamic linking and loading of objects into SRAM this functionality is not needed.

It is possible to implement it using a *mmap* which
simulates memory mapping of the object and returns the location. Beware that a real *mmap* is not possible on a MMU-less
architecture.


Extents
-------------

The whole object needs to be stored in a single contiguous extent (`<https://en.wikipedia.org/wiki/Extent_(file_systems)>`_)
in the backend storage in order to be able to be executed. This is the only method of object storage block allocation
that needs to be supported.


Metadata
--------------

It is proposed to avoid saving any metadata about the object store contents other than the data already contained
withing the ELF files themselves. This makes the object store data structure extremely simple and writable without
any special tools. A linker generated ELF file is a valid object store image (containing only a single object).
However, it is possible to use unused fields in the ELF header to save caching information about the object itself.
The implementation is free to use any padding bytes starting at the offset ``0x09`` up to ``0x0f``, that is 7 bytes
total. Note that if the implementation checks ELF signatures, it *must* do so with those padding bytes set to ``0x00``.

.. note::

	This caching information may contain, among others:

	- single byte hash of the ELF object name to speed up linear search for a specific object (calling open())
	- flag saying whether the object has been relocated to the current position

The prefered standard way of saving vendor specific metadata in ELF files is using *note* sections like described
in `<https://www.netbsd.org/docs/kernel/elf-notes.html>`_. For the list of notes present in the plumCore framework
generated ELF files see TBD.

Object store implementation *must* understand notes in the ``.note.plumcore.version`` section, namely:

=============== ================= =================================================
Name/vendor     Type              Description
=============== ================= =================================================
``plumCore``    ``0x00000001``    Object name without any version information
``plumCore``    ``0x00000002``    Object version as a string (SemVer 2.0 format)
``plumCore``    ``0x00000003``    Version metadata (SemVer 2.0 format)
``plumCore``    ``0x00000004``    Build date (ISO8601 format)
=============== ================= =================================================



Preallocation
-----------------

If the stored objects are not truly position independent, it must be possible to preallocate an object of a defined
size, *mmap* it to get its position in the backend storage memory and *relocate* the object to the obtained address.
The object is then written to the object store modified and ready to be run from the new location.


Defragmentation
-----------------------

During updates, new objects are written to the object store until there is no space left. Old objects can be deleted
to free some space, however depending on the exact usage pattern and object sizes, it may not be possible to find
a free contiguous extent of the required size. When this happens a defragmentation process can be run either
automatically or on request. The process is free to move all objects which are marked as not relocated and not being
executed. Support for moving of already relocated objects is not required nor possible as the object store service
doesn't have enough information to re-relocate objects after being moved.

.. note::

	The section mentions marking objects as *relocated* and *running*. As we are avoiding keeping any metadata
	in the object store, a way of determining the object state needs to be found.
	Object relocation can be determined by comparing the current object location within the object store with
	the ELF's ``.text`` section ``phdr->paddr`` address.
	The *running* (*being executed*) state is a bit more complicated. The list of running threads needs to be
	traversed to find a match with the ELF ``elf_header->entry``. A more straightforward approach is to keep
	a list of open objects and consider all running.


Supported backing storage
----------------------------

Block storage should be supported to allow loading from eMMC/SD memories/cards.
Flash storage must be supported including the XIP functionality to allow loading and executing from the internal flash
memory and optionally from memory mapped external flash memories (parallel flash, QSPI/octoSPI flash).


Simple block allocation
-----------------------------

The object store operates the backing storage with equally sized erasable blocks. The blocks are all the same size.
They are not divided into smaller write blocks nor any subblock allocation is done.
An erase block is either unused (erased) or used.


Basic data structure
-------------------------

ELF objects are saved in the backing storage as-is (verbatim) and aligned to the block size. A block is free if it
contains 0xffffffff as the first word, it is used if it contains a valid ELF header or is invalid otherwise.

If the block is used, the ELF header is parsed to determine the object size. All subsequent blocks of the same object
are ignored when searching for the next object.



Resource considerations
=============================

For a statically compiled plumCore, exactly one ELF object is needed to be saved, optionally a second one as a backup.
When doing updates, the original object(s) is either overwritten or a new object is appended at the end. It is expected
that object count will be less than 5 in most use cases. When dynamically loading plumCore components, the object count
is expected to be in the order of 20-100 ELF objects. For the considered number of saved objects in the store
it is sufficient to perform simple linear search for objects.

As the service implementing the object store is going to be used with the bootloader application, the code size must
be kept at minimum.

The implementation should avoid saving any state about objects in the store. Getting the *running* state of the object
is TBD.

