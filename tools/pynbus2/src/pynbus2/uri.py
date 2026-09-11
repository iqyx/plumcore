# SPDX-License-Identifier: GPL-3.0-or-later
#
# Connection URI parsing
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Parsing of the connection URIs that identify an nbus2 service.

A URI selects the transport (its scheme) and, optionally, a default destination service. The generic
shape is::

    <scheme>://<host>[:<port>][/<sid>[/<ep>]][?<key>=<value>&...]

The host, port, path and query are interpreted by the individual transport. The path may carry a
default destination as ``/<sid>`` or ``/<sid>/<ep>``; when present, :func:`pynbus2.connect` hands back
sockets already connected to it.
"""

from urllib.parse import urlsplit, parse_qs

from .errors import UriError


class Uri:
	"""A parsed connection URI."""

	def __init__(self, scheme, host, port, path, path_segments, query):
		self.scheme = scheme
		self.host = host
		self.port = port
		self.path = path
		self.path_segments = path_segments
		self.query = query

	def get(self, name, default=None):
		"""Return a single query parameter value, or ``default`` if absent."""
		values = self.query.get(name)
		if not values:
			return default
		return values[-1]

	@property
	def dst_sid(self):
		"""The default destination service ID from the path, or ``None``."""
		return self.path_segments[0] if len(self.path_segments) >= 1 else None

	@property
	def dst_ep(self):
		"""The default destination endpoint from the path, or ``None``."""
		return self.path_segments[1] if len(self.path_segments) >= 2 else None


def parse_uri(uri):
	"""Parse a connection URI string into a :class:`Uri`."""
	if not isinstance(uri, str):
		raise UriError('the connection URI must be a string')

	split = urlsplit(uri)
	if not split.scheme:
		raise UriError('the connection URI must start with a scheme, e.g. "udp6://..."')

	try:
		host = split.hostname
		port = split.port
	except ValueError as e:
		# A colon-separated device address (e.g. a BLE MAC, AA:BB:CC:DD:EE:FF) is not a valid
		# host:port and makes urlsplit's port parsing fail. As an unbracketed authority with more
		# than one colon can never be host:port (an IPv6 literal would be bracketed), hand the whole
		# authority to the transport as the host instead of rejecting the URI.
		authority = split.netloc.rpartition('@')[2]
		if authority.count(':') < 2:
			raise UriError('invalid host or port in the connection URI: %s' % e)
		host = authority
		port = None

	segments = [s for s in split.path.split('/') if s]
	query = parse_qs(split.query, keep_blank_values=True)

	return Uri(split.scheme, host, port, split.path, segments, query)
