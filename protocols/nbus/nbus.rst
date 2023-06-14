=========================================
nwDAQ CAN communication protocol (nbus)
=========================================

- request/response and publish/subscribe communication patterns
- dynamic network topology with CAN bridges and nbus protocol switches
- ability to discover newly added devices
- network topology change discovery
- full documentation availability
- simple protocol, simple implementations
- simple and secure cryptography
- support for transmitting arbitrary sized datagrams without guarantee of delivery


Network topology
============================

The basic physical network topology is a bus. To overcome large distances, one or multiple bridges can be employed
between individual bus segments. To allow star topology, nbus protocol switches can be used. They are basically briges
with more than two ports.

Devices on the network are called *nodes*. A node may communicate over multiple *channels*. A channel has multiple
*endpoints*.

Logical addressing
===============================

To keep things simple, let's forget about *devices*. They are opaque to the fundamental function of the protocol.
We will consider *channels* only.

A *channel* is a bidirectional stream of datagrams on the network. To identify a channel we are using a *channel identifier*.
It is an arbitrary octet stream at least 16 octets long (128 bits). It may be generated from a long-living public key,
UUID of some sort, etc. It is called a *full-id*.

However, for the purpose of channel advertisement and/or discovery, such long ID cannot be fit inside a CAN message.
It is shortened to 32 bits and computed as a cropped blake2s() hash of the *full-id*. It is called a *short-id*.

nbus uses CAN 2.0b (CAN-classic) or CAN-FD frames with 29 bit identifiers which means even the *short-id* cannot be used
as a CAN ID to communicate. A *channel-id* is needed, it is a 16 bit number set to zero when the channel is created.
A zero channel-id is considered invalid and is marked as such immediatelly. When a channel-id is marked invalid, it is
recomputed as a blake2s() hash of the *full-id* concatenated with the current *channel-id*, cropped to 16 bits.


To-do section
===========================

Minimum channel descriptor contents
===========================================

To properly work with channel discovery, maintain a device tree and assign host-side protocol drivers, it is required
that channel descriptors contain at least a minimal set of values. This minimal set is mandatory.

.. confval:: name
	:type: string
	:default: empty string

	A machine-readable name of the channel. The name MUST contain only alphanumeric characters, underscore and
	a hyphen. The name SHOULD be at most 16 characters long. Maximum length is not constrained (MTU limits it
	practically). The host MUST process names of up to 16 characters of length without modification. For longer
	names it is allowed to modify them. The modification should be made by cropping the name to 11 characters
	and appending a hyphen with 4 lowercase HEX characters encoding a cropped result of a PRF of the whole name.
	Examples are ``fs-system`` or ``some-long-n-4fc1``.

..confval:: parent
	:type: string
	:default: empty string
