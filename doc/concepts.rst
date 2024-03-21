================================================
Concepts and design goals
================================================


Thin wrappers for external libraries
===========================================

Whenever an external library providing a common service is used, it is wrapped in a thin
wrapper exposing a defined Interface. For example, filesystem access libraries are all
wrapped to provide the same ``Fs`` interface.


The less supported platforms and compilers, the better
==========================================================

plumCore is not aiming to be the most universal and the most widely used framework
among all industries. To achieve our goals, we need fully featured MCUs with capable
peripherals. Hence, the list of supported platforms is pretty short:

- ARM Cortex-M, namely Cortex-M4, Cortex-M7 and possibly newer

Sorry, no ESP with weird cores. No AVR, it is 2023 and the age of 8 bits is over.
No RISC-V either as there is simply no MCU with a RISC-V core mature enough yet.
PlumCore will never be FreeRTOS-style bloatware with millions of supported architectures
and processors.

Most of the codebase is in C. Again, we are in 2023, so no C89 support. Do not even
expect the code to compile as C99, we are moving to C23 (ISO/IEC 9899:2024) soon.
That means the only supported compiler now is GCC, LLVM is considered in the future
as an option.


Strict pattern usage
==============================

We have strictly defined patterns for most of the codebase. As an example, a code with
a requirement "the user is required to implement those 3 weak functions to provide
access to the flash memory" is simply not valid. If a library/service wants to access
a flash memory, it has to use a proper interface to access it, ``Flash`` in this case.


Strict coding style
===========================

We are expecting a coding style to be strictly adhered to. Yes, we do want curly braces
around single statements in ``if``. Yes, we have a mandatory ``default`` in a ``switch``
statement. Yes, we are using ``/* .. */`` as comments.

Why? Reading and writing the code should be a pleasure. Every developer with a healthy
dose of OCPD should be able to conform to basic coding guidelines.


Microkernel usage
=====================

We are moving towards the microkernel architecture where the kernel has only some
essential parts:

- context switcher
- scheduler
- basic IPC

All other components are implemented as services. Currently, this is not the case.
We are using FreeRTOS and some of its functionality which will go away in the future.


Code and functionality organisation
=========================================================

All functionality is split into small :ref:`services`. A service is a bunch of code, optionally
protected and/or sandboxed, using strictly defined interfaces to communicate with other
services (that is, to access or provide access for) and creating threads to do some actual
work.

There is a special kind of services called applications. As the name suggests, they glue
parts of the system together to make some use of it to achieve a goal. The same application
can run on different ports and a single port can support multiple applications, even running
concurrently.

There is also a service implementing the CLI (*command line interface*) which is accessible
usually using a diagnostic console. Optionally it can be remotely accessible.


Strict rules for passing dependencies
===========================================

There are only two patterns for passing dependencies among services - *dependency injection*
and *service locator*. When service instances are created statically, dependency injection is
used to inject references to interfaces of other services. The service is also allowed to
discover some common dependencies on its own using a service locator, which is again implemented
as a service.


Message queue data passing
====================================

The framework contains a message broker service for passing data between services.
The preferred way of manipulating data is to subscribe, receive the data, manipulate
and publish back with a different topic name.

The data is always passed in a common datatype resembling NumPy's ``ndarray``.


Selecting the best from all worlds
=======================================

plumCore uses a bit of a naive approach, because it is easy. It uses a bit of *Misra's*
approach, because it is supposedly safe. It requires all functions to return a result
of their execution and you are recommended to check it every time. We are not banning
dynamic memory allocation, but we have strict rules for it. We are programming deffensively,
but not too much to be contraproductive.


VCS and build system
=======================

Source code of plumCore is kept in a git repository on Github. GitFlow workflow is used for
development together with SemVer 2.0 for versioning. Kconfig is used for compile-time
configuration. Sources are built with scons.
