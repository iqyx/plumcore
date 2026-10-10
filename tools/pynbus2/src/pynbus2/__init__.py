# SPDX-License-Identifier: GPL-3.0-or-later
#
# pynbus2 - Python client library for the plumCore nbus2 messaging bus
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""pynbus2 - connect to a plumCore nbus2 service over a URI and exchange datagrams on sockets.

    import pynbus2

    with pynbus2.connect('udp6://') as nbus:
        sock = nbus.socket('00010002', 3)
        reply = sock.request(b'ping')
"""

from .nbus import connect, probe, Nbus, NbusSocket
from .errors import NbusError, UriError, NotConnectedError
from .transport import register
from .blake2s_siv import derive_keys
from . import packet
from . import advertisement

__version__ = '0.2.0'

__all__ = [
	'connect',
	'probe',
	'Nbus',
	'NbusSocket',
	'NbusError',
	'UriError',
	'NotConnectedError',
	'register',
	'derive_keys',
	'packet',
	'advertisement',
	'__version__',
]
