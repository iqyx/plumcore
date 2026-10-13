# SPDX-License-Identifier: GPL-3.0-or-later
#
# Transport abstraction for reaching an nbus2 service
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Transport-independent interface used by the high level :mod:`pynbus2.nbus` API.

A :class:`Transport` represents a way to reach the nbus2 network (e.g. a UDP/IPv6 bridge). It is a
factory for :class:`SocketBackend` objects, each of which carries datagrams to and from a single
remote endpoint. New connection methods are added by subclassing :class:`Transport` and registering
the subclass for a URI scheme in :mod:`pynbus2.transport`.
"""

import abc

from ..errors import NbusError

#: Query parameters only describing a URI for display (see :meth:`Transport.probe`), not selecting a connection.
DISPLAY_PARAMS = ('p', 'pv', 'name')


def connection_key(uri, *extra):
	"""A hashable key identifying the connection ``uri`` names: its scheme, host, port and query without the
	display only parameters, followed by ``extra``."""
	query = tuple(sorted((k, tuple(v)) for k, v in uri.query.items() if k not in DISPLAY_PARAMS))
	return (uri.scheme, uri.host, uri.port, query) + extra


class SocketBackend(abc.ABC):
	"""A single bidirectional datagram channel towards one nbus2 endpoint."""

	#: Suggested per-attempt reply timeout (the retransmit interval) for request/response callers.
	#: Slower media (e.g. a BLE link) raise this so a reply in flight is not treated as lost.
	request_timeout = 0.10

	@abc.abstractmethod
	def bind(self, id, ep):
		"""Set the local source address (sid, ep) used for outgoing datagrams."""

	@abc.abstractmethod
	def connect(self, id, ep):
		"""Set the remote destination (sid, ep) this socket sends to and receives from."""

	@abc.abstractmethod
	def send(self, data):
		"""Send one datagram payload to the connected destination."""

	@abc.abstractmethod
	def recv(self, timeout):
		"""Receive one datagram payload, or return ``None`` if ``timeout`` seconds elapse."""

	def flush(self):
		"""Discard any buffered inbound datagrams so a fresh request starts from a clean state.

		Returns the number of datagrams discarded (useful as instrumentation: a non-zero count means
		stale replies were left over from an earlier, retransmitted request). The default is a no-op
		returning 0; transports that queue received datagrams (e.g. BLE notifications) override it.
		"""
		return 0

	@abc.abstractmethod
	def close(self):
		"""Release the underlying resources."""


class Transport(abc.ABC):
	"""A factory for :class:`SocketBackend` objects over a particular medium."""

	#: Optional default destination (sid, ep) parsed from the URI, used by :func:`pynbus2.connect`
	#: to hand back already-connected sockets. ``None`` when the URI carries no destination.
	default_dst = None

	@classmethod
	@abc.abstractmethod
	def from_uri(cls, uri):
		"""Build a transport from a parsed :class:`pynbus2.uri.Uri`."""

	@classmethod
	def probe(cls, uri):
		"""Enumerate more complete connection URIs reachable from the (possibly incomplete) ``uri``.

		``uri`` is a parsed :class:`pynbus2.uri.Uri` whose scheme has already selected this transport;
		its remaining parts (host, destination) may still be missing. The transport inspects what is
		present and returns a list of URI *strings* filling in the next level of detail using whatever
		discovery method is specific to it. E.g. a serial transport probed with just its scheme lists
		the serial ports (``dgtext+serial:///dev/ttyUSB0`` ...), probed with a port it lists the
		services advertising behind it. The returned URIs may themselves be incomplete: probe again with
		one of them to refine further, or pass a complete one to :func:`pynbus2.connect`. A URI naming
		a service may carry its protocol name and version as ``p`` and ``pv`` query parameters, for
		display only. Returns an empty list when there is nothing to enumerate.

		Probing may block (opening a port, listening for advertisements, connecting a BLE device), so
		interactive callers should run it off their UI thread. The default enumerates nothing.
		"""
		return []

	@classmethod
	def split_uri(cls, uri):
		"""Split a parsed :class:`pynbus2.uri.Uri` into the connection it names and its destination.

		Returns ``(key, dst)``. URIs differing only in their destinations give the same hashable ``key``, so
		the sockets to all of them can share one transport. ``dst`` is the (sid, ep) destination, or ``None``
		when the URI carries none. The default takes the destination from the path (``/<sid>[/<ep>]``),
		transports using the path for something else override it.
		"""
		dst = (uri.dst_sid, uri.dst_ep) if uri.dst_sid is not None else None
		return connection_key(uri), dst

	def walk(self):
		"""Discover the interfaces reachable over this transport.

		Returns a list of (name, desc) tuples, desc being a map describing the interface (protocol "p",
		protocol version "pv", "mtu" and maximum sessions "ms"). Only transports exporting named interfaces
		(rible, swd) support it.
		"""
		raise NbusError('the transport does not support interface discovery')

	@abc.abstractmethod
	def open_socket(self):
		"""Create a new, unconnected :class:`SocketBackend` on this transport."""

	def close(self):
		"""Release any resources shared across the transport's sockets."""
