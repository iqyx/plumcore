# SPDX-License-Identifier: GPL-3.0-or-later
#
# Transport registry keyed by URI scheme
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Registry mapping a URI scheme to the :class:`~pynbus2.transport.base.Transport` that handles it.

Additional connection methods register themselves here. Only the UDP/IPv6 bridge transport is
provided today; a direct serial transport (which would have to implement the nbus2 wire crypto) is a
possible future addition.
"""

from ..errors import UriError
from .base import Transport, SocketBackend
from .udp6 import Udp6Transport
from .serial_dgtext import SerialDgtextTransport

_REGISTRY = {
	'udp6': Udp6Transport,
	'dgtext+serial': SerialDgtextTransport,
}


def register(scheme, transport_cls):
	"""Register a transport class for a URI scheme."""
	_REGISTRY[scheme] = transport_cls


def build_transport(uri):
	"""Instantiate the transport selected by a parsed :class:`~pynbus2.uri.Uri`."""
	transport_cls = _REGISTRY.get(uri.scheme)
	if transport_cls is None:
		raise UriError('unsupported transport scheme %r (known: %s)'
		               % (uri.scheme, ', '.join(sorted(_REGISTRY))))
	return transport_cls.from_uri(uri)


__all__ = ['Transport', 'SocketBackend', 'register', 'build_transport']
