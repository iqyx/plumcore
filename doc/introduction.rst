============================
Introduction
============================

The project was started back in 2015 when it was clear that IoT is going to emerge soon.
It was still a pre-LoRa era and the requirements and expectations were a bit fuzzy.
The main goal at that time was to provide a *WSN*-like (Wireless Sensor Networks) connectivity
to low power nodes. But as with IoT goes, it is mainly characterised by optimistic
programming, shallow learning curve frameworks and the maker community. PlumCore's
goals were the direct opposite.

plumCore is a C framework using the microkernel/services pattern. FreeRTOS
scheduler is used as the microkernel providing task scheduling and a basic IPC.
All the rest is implemented as modular services. A plumCore service provides
interfaces used by other services. Interface dependencies are either discovered
in runtime (service locator pattern) or injected (dependency injection). Even
the service locator is implemented as a service. Most of the code looks like
and behaves in an OOP manner. As an user interface, a tree-structured CLI
(command line interface) is available to configure the device.

plumCore use cases are various remotely operated measurement/DAQ systems with
optional on-site data processing (*edge computing*)
