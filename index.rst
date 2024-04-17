====================================
|plum| The plumCore DAQ framework
====================================

A modular framework for data logging and remote data acquisition.

The name represents a seed found in a plum fruit - the "core" of the plum.
Although not used in this manner it sounds sufficiently good to be used
as a name for a totally unrelated project.

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


.. toctree::
	:numbered:

	doc/introduction
	doc/concepts
	microkernel/index
	services/services
	services/interfaces/index
	applications/applications
	applets/applets
	protocols/nbus/nbus
	ports/index
	doc/conceptual/conceptual
	doc/code-structure
	doc/coding-style


