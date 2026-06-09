# SPDX-License-Identifier: GPL-3.0-or-later
#
# pynbus2 exception types
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.


class NbusError(Exception):
	"""Base class for all errors raised by pynbus2."""


class UriError(NbusError):
	"""The connection URI is malformed or names an unsupported transport."""


class NotConnectedError(NbusError):
	"""A send/receive was attempted on a socket that was never connected to a destination."""
