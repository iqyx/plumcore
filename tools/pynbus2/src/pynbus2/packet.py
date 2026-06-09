# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 packet encode/decode with pluggable protection schemes
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Assembly and parsing of whole nbus2 packets.

Packet layout (see services/nbus2/nbus2.c)::

    offset  size  field
    0       8     SIV (synthetic IV / 64-bit MAC tag), in the clear
    8       2     magic 'n2'                 ┐
    10      2     payload length (big endian) │
    12      2     tx counter (big endian)     │ 16-byte fixed header
    14      1     (dst_ep << 4) | src_ep      │  (encrypted)
    15      1     flags                       │
    16      4     destination id              │
    20      4     source id                  ┘
    24      ...   payload                       (encrypted)

The wire protection is pluggable: a *scheme* knows how to ``protect`` a freshly assembled packet and
how to ``unprotect`` (decrypt + authenticate) a received one. :func:`decode` tries a list of schemes
in order, so a receiver can accept several protections at once - newest first - for backward
compatibility while senders pick a single scheme. Today only :data:`BLAKE2S_SIV` is implemented; a
legacy ChaCha20+halfsiphash scheme can be appended once its cipher is available (no standard library
provides ChaCha20 with a 128-bit key).
"""

import collections

from . import blake2s_siv

HEADER_LEN = 24
_MAGIC = b'n2'

#: A decoded packet: 4-byte ids, endpoints and the raw payload.
Packet = collections.namedtuple('Packet', 'dst_id dst_ep src_id src_ep payload')


def _assemble(payload, dst_id, dst_ep, src_id, src_ep, counter):
	"""Build the cleartext packet buffer with the fixed header filled in and the SIV slot blank."""
	plen = len(payload)
	buf = bytearray(HEADER_LEN + plen)
	buf[8:10] = _MAGIC
	buf[10] = (plen >> 8) & 0xff
	buf[11] = plen & 0xff
	buf[12] = (counter >> 8) & 0xff
	buf[13] = counter & 0xff
	buf[14] = ((dst_ep & 0x0f) << 4) | (src_ep & 0x0f)
	buf[15] = 0
	buf[16:20] = dst_id
	buf[20:24] = src_id
	buf[24:] = payload
	return buf


def _parse(body):
	"""Validate a decrypted body (buf[8:]) and return a :class:`Packet`, or ``None`` if malformed.

	``body`` must already be authenticated by the caller; this only checks the magic and length and
	splits out the fields.
	"""
	if len(body) < 16 or body[0:2] != _MAGIC:
		return None
	plen = (body[2] << 8) | body[3]
	if (16 + plen) > len(body):
		return None
	return Packet(
		dst_id=bytes(body[8:12]),
		dst_ep=body[6] >> 4,
		src_id=bytes(body[12:16]),
		src_ep=body[6] & 0x0f,
		payload=bytes(body[16:16 + plen]),
	)


class Blake2sSiv:
	"""BLAKE2s-only SIV-mode protection (see :mod:`pynbus2.blake2s_siv`)."""

	name = 'blake2s-siv'

	@staticmethod
	def protect(buf, ke, km):
		"""Authenticate then encrypt the assembled packet ``buf`` in place."""
		tag = blake2s_siv.siv(km, bytes(buf[8:]))
		buf[0:8] = tag
		buf[8:] = blake2s_siv.crypt(ke, tag, bytes(buf[8:]))

	@staticmethod
	def unprotect(raw, ke, km):
		"""Decrypt and authenticate received bytes; return a :class:`Packet` or ``None`` on failure."""
		if len(raw) < HEADER_LEN:
			return None
		tag = raw[0:blake2s_siv.TAG_LEN]
		body = blake2s_siv.crypt(ke, tag, raw[8:])
		packet = _parse(body)
		if packet is None:
			return None
		# The SIV authenticates the cleartext header+payload; recompute and compare in constant time.
		cleartext = body[:16 + len(packet.payload)]
		if not _equal(blake2s_siv.siv(km, cleartext), tag):
			return None
		return packet


def _equal(a, b):
	"""Constant-time comparison of two short byte strings."""
	if len(a) != len(b):
		return False
	diff = 0
	for x, y in zip(a, b):
		diff |= x ^ y
	return diff == 0


#: The default protection schemes a receiver accepts, newest first.
DEFAULT_SCHEMES = (Blake2sSiv,)


def encode(payload, dst_id, dst_ep, src_id, src_ep, counter, ke, km, scheme=Blake2sSiv):
	"""Build, authenticate and encrypt one nbus2 packet under ``scheme``, returning the wire bytes."""
	buf = _assemble(payload, dst_id, dst_ep, src_id, src_ep, counter)
	scheme.protect(buf, ke, km)
	return bytes(buf)


def decode(raw, ke, km, schemes=DEFAULT_SCHEMES):
	"""Decrypt and authenticate one received packet, trying each scheme in order (newest first).

	Returns the :class:`Packet` from the first scheme that authenticates, or ``None`` if none do.
	"""
	for scheme in schemes:
		packet = scheme.unprotect(raw, ke, km)
		if packet is not None:
			return packet
	return None
