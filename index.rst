The plumCore (IoT) framework
====================================

A modular framework for data logging, remote data acquisition
and low power wireless mesh networks.

The name represents a seed found in a plum fruit - the core of the plum.
Although not used in this manner it sounds sufficiently good to be used
as a name for a totally unrelated project.


Introduction
---------------------------

plumCore is a C framework using the microkernel/services pattern. FreeRTOS
scheduler is used as the microkernel providing task scheduling and a basic IPC.
All the rest is implemented as modular services. A plumCore service provides
interfaces used by other services. Interface dependencies are either discovered
in runtime (service locator pattern) or injected (dependency injection). Even
the service locator is implemented as a service. Most of the code looks like
and behaves in an OOP manner. From the user standpoint, a tree-structured CLI
(command line interface) is available to configure the device.

plumCore is not meant to be run on dataloggers powered by primary batteries
lasting for years. It is not very-low-power friendly as it is not its design
goal. It is intended to be used in medium power applications with power
requiremens in the tens to hundreds of milliwats range where the actual power
consumption of the MCU itself doesn't play a significant role. This is the case
of various remotely operated measurement/DAQ systems with optional on-site data
processing. The main target application during the development are modular
devices for the natWatch project, most of them are powered by solar energy.








