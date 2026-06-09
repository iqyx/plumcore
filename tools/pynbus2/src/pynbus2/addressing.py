# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 address parsing and formatting helpers
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Helpers for the nbus2 addressing model.

A node is identified by a 32 bit service ID (sid) and an endpoint (ep, 4 bits, 0..15). Throughout
the library a sid is normalised to 4 raw bytes (big endian) and accepted from the user as a hex
string (``"00010002"``, optionally ``0x``-prefixed or colon-grouped), an ``int`` or 4 raw ``bytes``.
"""


def parse_id(value):
	"""Normalise a service ID given as a hex string, int or 4 bytes into 4 big-endian bytes."""
	if isinstance(value, (bytes, bytearray)):
		if len(value) != 4:
			raise ValueError('a service ID must be exactly 4 bytes')
		return bytes(value)
	if isinstance(value, int):
		if not 0 <= value <= 0xffffffff:
			raise ValueError('a service ID must fit into 32 bits')
		return value.to_bytes(4, 'big')

	text = str(value).strip().lower()
	if text.startswith('0x'):
		text = text[2:]
	text = text.replace(':', '')
	if not text:
		raise ValueError('empty service ID')
	number = int(text, 16)
	if not 0 <= number <= 0xffffffff:
		raise ValueError('a service ID must fit into 32 bits')
	return number.to_bytes(4, 'big')


def parse_ep(value):
	"""Normalise an endpoint to an int in the range 0..15."""
	ep = int(value)
	if not 0 <= ep <= 15:
		raise ValueError('an endpoint must be in the range 0..15')
	return ep


def format_id(id_bytes):
	"""Format a 4 byte service ID as a plain 8 character hex string (no separators)."""
	return id_bytes.hex()
