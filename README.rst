.. image:: doc/static/header.png
	:width: 100%
	:alt: header image

.. raw:: html

   <h1><img style="position: relative; top: -0.15em; height: 1em; width: auto;" src="doc/static/plum.svg"> The plumCore DAQ framework</h1>


A modular framework for data logging and remote data acquisition.

The name represents a seed found in a plum fruit - the "core" of the plum.
Although not used in this manner it sounds sufficiently good to be used
as a name for a totally unrelated project.

.. raw:: html

   <p>
	Current develop branch <img src="https://github.com/iqyx/plumcore/actions/workflows/main.yml/badge.svg?branch=develop">
   </p>

Introduction
=====================

plumCore is a C framework using the microkernel/services pattern. FreeRTOS
scheduler is used as the microkernel providing task scheduling and a basic IPC.
All the rest is implemented as modular services. A plumCore service provides
interfaces used by other services. Interface dependencies are either discovered
in runtime (service locator pattern) or injected (dependency injection). Even
the service locator is implemented as a service. Most of the code looks like
and behaves in an OOP manner. As an user interface, a tree-structured CLI
(command line interface) is available to configure the device.

plumCore use cases are various remotely operated measurement/DAQ systems with
optional on-site data processing (edge computing). IoT and consumer hardware
are not plumCore's target applications.


Concepts and design goals
================================================

- Thin wrappers for external libraries - whenever an external library providing
  a common service is used, it is wrapped in a thin wrapper exposing a defined Interface.

- The less supported platforms and compilers, the better - plumCore is not aiming
  to be the most universal and the most widely used framework among all industries.
  To achieve our goals, we need fully featured MCUs with capable peripherals. Hence,
  the list of supported platforms is pretty short.

- Strict pattern usage and coding style

- Microkernel/services architecture - we are moving towards the microkernel architecture
  where the kernel has only some essential parts and all other components are implemented
  as services. Currently, this is not the case. We are using FreeRTOS and some of its
  functionality which will go away in the future.

- Dynamic ELF loading of the framework components (services)

- Strict code and functionality organisation - services, applications, jobs, applets

- Strict rules for passing dependencies - there are only two patterns for passing
  dependencies among services - *dependency injection* and *service locator*

- Message queue data passing - the framework contains a message broker service for passing
  data between services. The data is always passed in a common datatype resembling NumPy's ``ndarray``.

- Selecting the best from all worlds - plumCore uses a bit of a naive approach, because it is easy.
  It uses a bit of *Misra's* approach, because it is supposedly safe. It requires all functions
  to return a result of their execution and you are recommended to check it every time. We are
  not banning dynamic memory allocation, but we have strict rules for it. We are programming
  defensively, but not too much to be contraproductive.


Current status
======================

The project is currently undergoing many changes and should be considered alpha-quality.
It contains many known defects and legacy code dating back to 2015. Even in these circumstances
it is being actively run on some commercial products in field.


Resources
======================

to-do:

- downloads
- host tools, flashing the firmware
- documentation
- list of hardware


Very quick-start guide
================================

Get the source code:

.. code::

   git clone --branch develop https://github.com/iqyx/plumcore.git

Create a python virtual environment and install the required packages:

.. code::

   python -m venv venv
   source venv/bin/activate
   pip install -r requirements.txt

Create a private Ed25519 key for signing the resulting ELF. Get the public key too:

.. code::

   python -c "import hashlib; import base64; print(base64.b64encode(hashlib.sha256(b'default-key').digest()).decode('utf-8'))" > elfsign.key
   scripts/elfsign.py --sk elfsign.key -p elfsign.pub

Build for the selected configuration, strip and sign the resulting ELF:

.. code::

   defconfig config/your_hardware_defconfig
   scons firmware-sign


Community
====================

to-do


Code management, contributing
====================================

Source code of plumCore is kept in a git repository on Github. GitFlow workflow is used for
development together with SemVer 2.0 for versioning. Kconfig is used for compile-time
configuration. Sources are built with scons.

Report bugs using `issues <https://github.com/iqyx/plumcore/issues>`_, contributions are welcome using pull requests.


Licensing
====================

All code is released under the `GNU GPL v3 (or later) <https://www.gnu.org/licenses/gpl-3.0.en.html>`_
license, see reasoning (to-do). There are parts of the project still licensed under the 2-clause BSD license,
it will change in the future.
