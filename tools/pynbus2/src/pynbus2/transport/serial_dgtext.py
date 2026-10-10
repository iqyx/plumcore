# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 over a serial line, framed with dgtext
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""The ``dgtext+serial`` transport: nbus2 packets over a serial port, framed as dgtext text lines.

See :mod:`pynbus2.transport.serial_base` for the URI form and discovery, and
:class:`~pynbus2.transport.framing.DgtextFramer` for the framing::

    dgtext+serial://<device>[/<sid>/<ep>]?baudrate=<n>&key=<preshared>&self=<sid>
"""

from .serial_base import SerialStreamTransport
from .framing import DgtextFramer


class SerialDgtextTransport(SerialStreamTransport):

	framer_class = DgtextFramer
