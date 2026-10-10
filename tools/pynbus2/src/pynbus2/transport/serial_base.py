# SPDX-License-Identifier: GPL-3.0-or-later
#
# Common base of the nbus2 serial line transports
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""What the serial transports (``dgtext+serial``, ``dgstream+serial``) share: URI handling and discovery.

The serial device occupies the URI path, so a default destination is carried as the last two path
components (``.../<sid>/<ep>``) with the device path in front of them::

    <scheme>://<device>[/<sid>/<ep>]?baudrate=<n>&key=<preshared>&self=<sid>

    <scheme>:///dev/ttyUSB0?baudrate=1000000          device only (note the triple slash)
    <scheme>:///dev/ttyUSB0/00010002/3?key=abcd       device plus default destination 00010002 ep 3
    <scheme>://COM3/00010002/3                        Windows device in the host position

The host performs the nbus2 BLAKE2s-SIV packet protection itself, so the ``key`` query parameter is
the preshared key (default ``abcd``, matching the firmware). ``self`` is our own service ID, used as
the source for sockets that are not explicitly bound. A concrete transport only supplies the framer.
"""

import time
from urllib.parse import urlencode

from .stream import StreamTransport, SerialStream
from ..addressing import parse_id, format_id
from ..blake2s_siv import derive_keys
from ..errors import UriError
from ..uri import split_serial_path

DEFAULT_BAUDRATE = 115200
DEFAULT_KEY = 'abcd'
DEFAULT_SELF_ID = '00000001'

#: How long probe() listens on a serial port for descriptor advertisements, in seconds. Services
#: advertise every 2 s (see services/nbus2/nbus2.c), so this catches a couple of rounds.
ADVERTISEMENT_WAIT = 5.0


def _service_uri(scheme, device, query, adv):
	"""Build a connectable service URI from a discovered advertisement.

	The destination service (sid, ep) goes in the path. The probed URI's own query (baudrate, key, ...)
	is kept, and the descriptor's protocol name/version, when present, are added as ``p``/``pv``.
	"""
	params = [(k, v) for k, values in query.items() if k not in ('p', 'pv') for v in values]
	params += [(k, v) for k, v in (('p', adv.protocol), ('pv', adv.protocol_version)) if v is not None]
	uri = '%s://%s/%s/%d' % (scheme, device, format_id(adv.src_id), adv.src_ep)
	return '%s?%s' % (uri, urlencode(params)) if params else uri


class SerialStreamTransport(StreamTransport):
	"""A :class:`StreamTransport` over a serial port; subclasses set :attr:`framer_class`."""

	#: The framer class wrapping the serial stream, constructed as ``framer_class(stream)``.
	framer_class = None

	@classmethod
	def _open(cls, uri, on_advertisement=None):
		device, dst = split_serial_path(uri)
		if not device:
			raise UriError('the %s URI must name a serial device, e.g. %s:///dev/ttyUSB0/<sid>/<ep>'
			               % (uri.scheme, uri.scheme))

		ke, km = derive_keys(uri.get('key', DEFAULT_KEY))
		stream = SerialStream(device, baudrate=int(uri.get('baudrate', DEFAULT_BAUDRATE)))
		transport = cls(stream, cls.framer_class(stream), ke, km, parse_id(uri.get('self', DEFAULT_SELF_ID)),
		                on_advertisement=on_advertisement)
		transport.default_dst = dst
		return transport

	@classmethod
	def from_uri(cls, uri):
		return cls._open(uri)

	@classmethod
	def probe(cls, uri):
		"""Enumerate serial ports, then the services advertising on one.

		Probed with just the scheme it lists the host's serial ports as ``<scheme>:///dev/ttyXXX``
		URIs. Probed with a port it opens it, listens for descriptor advertisements for
		:data:`ADVERTISEMENT_WAIT` seconds (see :mod:`pynbus2.advertisement`) and returns one URI per
		distinct advertised service::

		    <scheme>:///dev/ttyUSB0/<sid>/<ep>?p=<protocol>&pv=<version>
		"""
		device, _dst = split_serial_path(uri)
		if not device:
			try:
				from serial.tools import list_ports
			except ImportError as e:
				raise ImportError(
					'the serial transports require pyserial; install it with "pip install pyserial" '
					'or "pip install pynbus2[serial]"') from e
			return ['%s://%s' % (uri.scheme, port.device) for port in list_ports.comports()]

		found = {}
		transport = cls._open(uri, on_advertisement=lambda adv: found.setdefault((adv.src_id, adv.src_ep), adv))
		try:
			# The transport's reader thread collects the advertisements meanwhile.
			time.sleep(ADVERTISEMENT_WAIT)
		finally:
			transport.close()

		return [_service_uri(uri.scheme, device, uri.query, adv) for adv in found.values()]
