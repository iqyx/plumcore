# SPDX-License-Identifier: GPL-3.0-or-later
#
# UDP/IPv6 bridge transport
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""nbus2 transport over a UDP/IPv6 bridge.

This mirrors how ``tools/proto-conf.py`` and ``tools/proto-nbus-flash.py`` reach a device: a host
side bridge exposes the nbus2 network on a link-local-style IPv6 prefix where the lower 32 bits of an
address are the service ID and the UDP port is ``base_port + ep``. The bridge (and the device) handle
the nbus2 wire framing, medium access and packet protection, so nothing here touches crypto - each
datagram payload is carried verbatim.

URI form::

    udp6://[<local-bind-addr>][:<base-port>][/<sid>[/<ep>]]?prefix=<prefix>&self=<sid>&mtu=<bytes>

    udp6://                                 all defaults
    udp6://[fd00:dead:beef::1]:52000        explicit bind address and base port
    udp6:///00010002/3                      default bridge, default destination 00010002 ep 3
    udp6://?prefix=fd00:dead:beef::&self=1   override the network prefix and our own service ID
"""

import socket

from .base import Transport, SocketBackend
from ..addressing import parse_id, parse_ep, format_id

DEFAULT_PREFIX = 'fd00:dead:beef::'
DEFAULT_BASE_PORT = 52000
DEFAULT_SELF_ID = '00000001'
DEFAULT_MTU = 1024


def _id_to_addr(prefix, id_bytes):
	"""Build the IPv6 address of a service: the prefix with the sid in the lower 32 bits."""
	return '%s%s:%s' % (prefix, id_bytes[0:2].hex(), id_bytes[2:4].hex())


class Udp6Transport(Transport):

	def __init__(self, prefix=DEFAULT_PREFIX, base_port=DEFAULT_BASE_PORT, local_addr=None,
	             self_id=DEFAULT_SELF_ID, mtu=DEFAULT_MTU):
		self._prefix = prefix
		self._base_port = base_port
		self._self_id = parse_id(self_id)
		self._mtu = mtu
		# The local bind address defaults to our own service ID under the same prefix.
		self._local_addr = local_addr if local_addr is not None else _id_to_addr(prefix, self._self_id)

	@classmethod
	def from_uri(cls, uri):
		transport = cls(
			prefix=uri.get('prefix', DEFAULT_PREFIX),
			base_port=uri.port if uri.port is not None else DEFAULT_BASE_PORT,
			local_addr=uri.host,
			self_id=uri.get('self', DEFAULT_SELF_ID),
			mtu=int(uri.get('mtu', DEFAULT_MTU)),
		)
		# The path may carry a default destination as /<sid> or /<sid>/<ep>.
		if uri.dst_sid is not None:
			transport.default_dst = (uri.dst_sid, uri.dst_ep)
		return transport

	def open_socket(self):
		return Udp6Socket(self._prefix, self._local_addr, self._base_port, self._self_id, self._mtu)


class Udp6Socket(SocketBackend):

	def __init__(self, prefix, local_addr, base_port, self_id, mtu):
		self._prefix = prefix
		self._local_addr = local_addr
		self._base_port = base_port
		self._mtu = mtu

		self._local_id = self_id
		self._local_ep = 0
		self._remote_id = None
		self._remote_ep = 0

		self._sock = None

	def _ensure_socket(self):
		if self._sock is not None:
			return
		s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
		s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		if hasattr(socket, 'SO_REUSEPORT'):
			s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
		s.bind((self._local_addr, self._base_port + self._local_ep))
		self._sock = s

	def bind(self, id, ep):
		self._local_id = parse_id(id)
		self._local_ep = parse_ep(ep)
		self._local_addr = _id_to_addr(self._prefix, self._local_id)

	def connect(self, id, ep):
		self._remote_id = parse_id(id)
		self._remote_ep = parse_ep(ep)
		self._ensure_socket()
		self._sock.connect((_id_to_addr(self._prefix, self._remote_id), self._base_port + self._remote_ep))

	def send(self, data):
		if self._sock is None:
			raise OSError('socket is not connected to a destination')
		self._sock.send(data)

	def recv(self, timeout):
		if self._sock is None:
			raise OSError('socket is not connected to a destination')
		self._sock.settimeout(timeout)
		try:
			return self._sock.recv(self._mtu)
		except (socket.timeout, TimeoutError):
			return None

	def close(self):
		if self._sock is not None:
			self._sock.close()
			self._sock = None

	def __repr__(self):
		remote = format_id(self._remote_id) if self._remote_id is not None else '-'
		return '<Udp6Socket %s:%d -> %s:%d>' % (format_id(self._local_id), self._local_ep, remote, self._remote_ep)
