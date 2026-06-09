# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 over a serial line, framed with dgtext
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""The ``dgtext+serial`` transport: nbus2 packets over a serial port, framed as dgtext text lines.

Unlike the UDP/IPv6 bridge, the serial device occupies the URI path, so a default destination is
carried as the last two path components (``.../<sid>/<ep>``) with the device path in front of them::

    dgtext+serial://<device>[/<sid>/<ep>]?baudrate=<n>&key=<preshared>&self=<sid>

    dgtext+serial:///dev/ttyUSB0?baudrate=1000000          device only (note the triple slash)
    dgtext+serial:///dev/ttyUSB0/00010002/3?key=abcd       device plus default destination 00010002 ep 3
    dgtext+serial://COM3/00010002/3                        Windows device in the host position

The host performs the nbus2 BLAKE2s-SIV packet protection itself, so the ``key`` query parameter is
the preshared key (default ``abcd``, matching the firmware). ``self`` is our own service ID, used as
the source for sockets that are not explicitly bound.
"""

from .stream import StreamTransport, SerialStream
from .framing import DgtextFramer
from ..addressing import parse_id, parse_ep
from ..blake2s_siv import derive_keys

DEFAULT_BAUDRATE = 115200
DEFAULT_KEY = 'abcd'
DEFAULT_SELF_ID = '00000001'


class SerialDgtextTransport(StreamTransport):

	@classmethod
	def from_uri(cls, uri):
		device, dst = cls._split_path(uri)
		if not device:
			raise ValueError('the dgtext+serial URI must name a serial device, '
			                 'e.g. dgtext+serial:///dev/ttyUSB0/<sid>/<ep>')

		ke, km = derive_keys(uri.get('key', DEFAULT_KEY))
		stream = SerialStream(device, baudrate=int(uri.get('baudrate', DEFAULT_BAUDRATE)))
		framer = DgtextFramer(stream)
		transport = cls(stream, framer, ke, km, parse_id(uri.get('self', DEFAULT_SELF_ID)))
		transport.default_dst = dst
		return transport

	@staticmethod
	def _split_path(uri):
		"""Split the URI into the serial device path and an optional ``(sid, ep)`` destination.

		The destination, when present, is the last two path components; the device path is the host
		(if any) followed by the remaining leading components. The two trailing components are only
		taken as a destination when they actually parse as a service ID and endpoint, so a deep
		device path (e.g. ``/dev/serial/by-id/...``) is left untouched.
		"""
		segments = list(uri.path_segments)
		dst = None
		if len(segments) >= 2:
			sid, ep = segments[-2], segments[-1]
			try:
				parse_id(sid)
				parse_ep(ep)
			except (ValueError, TypeError):
				pass
			else:
				dst = (sid, ep)
				segments = segments[:-2]

		if uri.host:
			device = '/'.join([uri.host] + segments)
		else:
			device = '/' + '/'.join(segments) if segments else ''
		return device, dst
