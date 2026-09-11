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

	@abc.abstractmethod
	def open_socket(self):
		"""Create a new, unconnected :class:`SocketBackend` on this transport."""

	def close(self):
		"""Release any resources shared across the transport's sockets."""
