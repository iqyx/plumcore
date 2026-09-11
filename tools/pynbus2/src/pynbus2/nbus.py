# SPDX-License-Identifier: GPL-3.0-or-later
#
# High level nbus2 connection and socket API
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""The user facing API: :func:`connect`, :class:`Nbus` and :class:`NbusSocket`.

Typical use::

    import pynbus2

    with pynbus2.connect('udp6://') as nbus:
        sock = nbus.socket('00010002', 3)
        sock.send(b'...')
        reply = sock.recv(timeout=0.5)

A default destination may instead be carried in the URI path (``udp6:///00010002/3``), in which case
``nbus.socket()`` returns a socket already connected to it.
"""

import logging

from .uri import parse_uri
from .transport import build_transport
from .errors import NotConnectedError

logger = logging.getLogger(__name__)


class NbusSocket:
	"""A single nbus2 socket: connect it to a destination, then send and receive datagrams."""

	def __init__(self, backend):
		self._backend = backend
		self._connected = False

	def bind(self, id, ep=0):
		"""Set the local source (sid, ep) used for outgoing datagrams. Returns ``self``."""
		self._backend.bind(id, ep)
		return self

	def connect(self, id, ep):
		"""Connect the socket to a destination (sid, ep). Returns ``self``."""
		self._backend.connect(id, ep)
		self._connected = True
		return self

	def send(self, data):
		"""Send one datagram payload to the connected destination."""
		if not self._connected:
			raise NotConnectedError('connect the socket to a destination before sending')
		self._backend.send(bytes(data))

	def recv(self, timeout=0.05):
		"""Receive one datagram payload, or ``None`` if nothing arrives within ``timeout`` seconds."""
		if not self._connected:
			raise NotConnectedError('connect the socket to a destination before receiving')
		return self._backend.recv(timeout)

	@property
	def request_timeout(self):
		"""The backend's suggested per-attempt reply timeout (the retransmit interval)."""
		return self._backend.request_timeout

	def request(self, data, timeout=0.05, retries=50):
		"""Send ``data`` and wait for a reply, retransmitting on timeout.

		Returns the first reply payload received, or ``None`` if no reply arrives after ``retries``
		attempts. This is the simple request/response pattern the nbus2 command tools rely on.
		"""
		payload = bytes(data)
		# Discard replies left over from an earlier request (e.g. a duplicate produced by a retransmit)
		# so this request is matched against a reply that actually answers it, not a stale one. A
		# non-zero drop count is a symptom worth surfacing: it means the previous exchange desynced.
		dropped = self._backend.flush()
		if dropped:
			logger.debug('flushed %d stale repl%s before request', dropped, 'y' if dropped == 1 else 'ies')
		for attempt in range(retries):
			self.send(payload)
			reply = self.recv(timeout)
			if reply is not None:
				if attempt:
					logger.debug('reply arrived after %d retransmit(s)', attempt)
				return reply
		logger.debug('no reply after %d attempt(s)', retries)
		return None

	def close(self):
		"""Close the socket and release its resources."""
		self._backend.close()
		self._connected = False

	def __enter__(self):
		return self

	def __exit__(self, *exc):
		self.close()


class Nbus:
	"""A connection to the nbus2 network over a single transport; a factory for sockets."""

	def __init__(self, transport, default_dst=None):
		self._transport = transport
		self._default_dst = default_dst
		self._sockets = []

	def socket(self, id=None, ep=None):
		"""Create a socket.

		With ``id`` (and optionally ``ep``) the socket is connected to that destination. Without
		arguments, a default destination taken from the URI path is used if present; otherwise the
		socket is returned unconnected and must be ``connect()``-ed manually.
		"""
		sock = NbusSocket(self._transport.open_socket())
		self._sockets.append(sock)

		if id is None and self._default_dst is not None:
			id, default_ep = self._default_dst
			if ep is None:
				ep = default_ep

		if id is not None:
			if ep is None:
				raise ValueError('an endpoint is required to connect a socket')
			sock.connect(id, ep)

		return sock

	def close(self):
		"""Close all sockets created on this connection and release the transport."""
		for sock in self._sockets:
			sock.close()
		self._sockets = []
		self._transport.close()

	def __enter__(self):
		return self

	def __exit__(self, *exc):
		self.close()


def connect(uri):
	"""Connect to an nbus2 service identified by ``uri`` and return an :class:`Nbus`."""
	transport = build_transport(parse_uri(uri))
	return Nbus(transport, transport.default_dst)
