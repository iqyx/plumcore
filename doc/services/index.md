# Services

In plumCore, being a microkernel architecture, most of the functionality is implemented as unprivileged user-space services. A service generally consists of a state (its assigned memory), threads with their corresponding stacks, one or more interfaces the service provides and a bunch of depenencies on interfaces provided by other services or the microkernel itself.

There are multiple groups of services for different purposes, albeit not strictly marked or split in any way.

Low level architecture-dependent services provide means of using or controlling systick or interrupts, SoC-dependent services allow accessing SoC's peripherals and communication interfaces, port-dependent services (although rare) may utilize some specific port information to control part of its hardware.

Higher level services never access hardware or SoC resources directly. They usually depend on interfaces provided by low level services. In the result they allow access to memories, filesystems, GNSS recivers, radio interfaces, etc.

There are even higher level services possible providing command line interfaces, user interfaces using displays, high level encrypted network connectivity, data-driven flow graph processing capability or any other functionality abstract enough to not depend on any low level specifics.

## Low level device drivers

## High level (second level) device drivers

## DAQ and data processing services

## Volume and filesystem access

## Communication protocols

## Communication server/clients

## User interface services

## Generic system services

