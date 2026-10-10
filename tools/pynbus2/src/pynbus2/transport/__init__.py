# SPDX-License-Identifier: GPL-3.0-or-later
#
# Transport registry keyed by URI scheme
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Registry mapping a URI scheme to the :class:`~pynbus2.transport.base.Transport` that handles it.

Additional connection methods register themselves here with :func:`register`. The transports import
their optional third party dependencies (pyserial, SimplePyBLE, pyocd, cbor2) only when used.
"""

from ..errors import UriError
from .base import Transport, SocketBackend
from .udp6 import Udp6Transport
from .serial_dgtext import SerialDgtextTransport
from .serial_dgstream import SerialDgstreamTransport
from .dgble import DgbleTransport
from .rible import RibleTransport
from .swd import SwdTransport

_REGISTRY = {
	'udp6': Udp6Transport,
	'dgtext+serial': SerialDgtextTransport,
	'dgstream+serial': SerialDgstreamTransport,
	'dgble': DgbleTransport,
	'rible': RibleTransport,
	'swd': SwdTransport,
}


def register(scheme, transport_cls):
	"""Register a transport class for a URI scheme."""
	_REGISTRY[scheme] = transport_cls


def schemes():
	"""The URI schemes that have a registered transport."""
	return sorted(_REGISTRY)


def transport_for(scheme):
	"""The transport class handling ``scheme``, or raise :class:`~pynbus2.errors.UriError` if none does."""
	transport_cls = _REGISTRY.get(scheme)
	if transport_cls is None:
		raise UriError('unsupported transport scheme %r (known: %s)'
		               % (scheme, ', '.join(sorted(_REGISTRY))))
	return transport_cls


def build_transport(uri):
	"""Instantiate the transport selected by a parsed :class:`~pynbus2.uri.Uri`."""
	return transport_for(uri.scheme).from_uri(uri)


__all__ = ['Transport', 'SocketBackend', 'register', 'schemes', 'transport_for', 'build_transport']
