============================
Introduction
============================


The project was started back in 2015 when it was clear that IoT is going to emerge soon.
It was still a pre-LoRa era and the requirements and expectations were a bit fuzzy.
The main goal at that time was to provide a *WSN*-like (Wireless Sensor Networks) connectivity
to low power nodes doing data acquisition in field. The term was later largely
abandoned and replaced with IoT, including a broader range of devices.
But as was shown by the following years, it is mainly characterised by optimistic
programming, fast learning curve frameworks and the maker community. PlumCore's
goals are the direct opposite and this is also the case the project is refusing
to be named an IoT framework.

plumCore is targetting developers of data acquisition systems. It is a C framework
for making your life easier. It has clear concepts and "dos and don'ts". It tells you
how and helps you to run your precious piece of code on a MCU sitting on a custom
or readily available hardware. It is using the microkernel/services pattern.
FreeRTOS scheduler is used as the microkernel providing task scheduling and a basic IPC.
All the rest is implemented as modular services meaning that all the responsibilities
in the system are defined and decoupled form each other. A plumCore service provides
interfaces used by other services and also expects something in return. This is called
a dependency. Interface dependencies are either discovered in runtime (service locator
pattern) or injected (dependency injection). Even the service locator is implemented
as a service. Most of the code looks like and behaves in an OOP manner. As an user
interface, a tree-structured CLI (command line interface) is available to configure
the device.

plumCore use cases are various remotely operated measurement/DAQ systems with
optional on-site data processing. Generally, it is well suited for use cases taking
care of and preserving the environment we all live in. It is meant to help us and not
to cause additional burden.
