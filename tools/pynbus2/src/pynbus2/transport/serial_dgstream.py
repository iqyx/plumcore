# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 over a serial line, framed by inter-frame gaps (dgstream)
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""The ``dgstream+serial`` transport: nbus2 packets over a serial port, delimited by idle gaps.

See :mod:`pynbus2.transport.serial_base` for the URI form and discovery, and
:class:`~pynbus2.transport.framing.DgstreamFramer` for the framing::

    dgstream+serial://<device>[/<sid>/<ep>]?baudrate=<n>&key=<preshared>&self=<sid>
"""

from .serial_base import SerialStreamTransport
from .framing import DgstreamFramer


class SerialDgstreamTransport(SerialStreamTransport):

	framer_class = DgstreamFramer
