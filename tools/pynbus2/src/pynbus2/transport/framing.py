# SPDX-License-Identifier: GPL-3.0-or-later
#
# dgtext and dgstream datagram framing over a byte Stream
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Framing of whole datagrams over a byte :class:`~pynbus2.transport.stream.Stream`.

Two framings are provided, matching the firmware services of the same name.

``proto-dgtext`` (:class:`DgtextFramer`) sends each datagram as one printable text line::

    $<base64 payload>*<checksum>\\n

where ``$`` is the sync token, the payload is the raw datagram base64-encoded, ``*`` separates the
trailing checksum, and the checksum is the first 4 bytes of a BLAKE2s digest of the *raw* payload as
lowercase hex (8 chars). The receiver discards everything until a ``$``; a stray ``$`` mid-line
resynchronises to the new frame. Every byte on the wire is printable ASCII, so it survives text-only
transports. See services/proto-dgtext/proto-dgtext.{h,rst}.

``proto-dgstream`` (:class:`DgstreamFramer`) sends the raw datagram verbatim in a single burst, with no
framing bytes, length prefix or escaping. A short idle period on the medium afterwards delimits it from
the next one, so the receiver accumulates bytes until a read times out. This pairs naturally with
half-duplex links (RS485, CAN PHY) where the line idles between bursts. See
services/proto-dgstream/proto-dgstream.{h,c}.
"""

import base64
import hashlib
import time

SYNC = b'$'
DELIM = b'*'
EOL = b'\n'

#: Number of BLAKE2s digest bytes used as the line checksum (PROTO_DGTEXT_CKSUM_BYTES).
CKSUM_BYTES = 4


def _checksum(payload):
	return hashlib.blake2s(payload, digest_size=CKSUM_BYTES).hexdigest().encode('ascii')


class DgtextFramer:
	"""Reads and writes datagrams as dgtext lines over an underlying :class:`~pynbus2.transport.stream.Stream`."""

	def __init__(self, stream, read_slice=0.05):
		self._stream = stream
		self._read_slice = read_slice
		self._rx = bytearray()

	def write_datagram(self, data):
		"""Encode one datagram as a dgtext line and write it to the stream."""
		line = SYNC + base64.b64encode(data) + DELIM + _checksum(data) + EOL
		self._stream.write(line)

	def read_datagram(self, timeout):
		"""Read one valid datagram, or return ``None`` if none arrives within ``timeout`` seconds.

		Malformed lines (bad base64, wrong length, checksum mismatch) are skipped; a stray sync token
		mid-line resynchronises to the latest frame start.
		"""
		deadline = (time.monotonic() + timeout) if timeout is not None else None
		while True:
			nl = self._rx.find(EOL)
			if nl >= 0:
				line = bytes(self._rx[:nl])
				del self._rx[:nl + 1]
				datagram = self._parse_line(line)
				if datagram is not None:
					return datagram
				continue

			slice_timeout = self._read_slice
			if deadline is not None:
				remaining = deadline - time.monotonic()
				if remaining <= 0:
					return None
				slice_timeout = min(slice_timeout, remaining)

			chunk = self._stream.read_some(slice_timeout)
			if chunk:
				self._rx += chunk

	@staticmethod
	def _parse_line(line):
		"""Parse one received line into a raw datagram, or ``None`` if malformed."""
		# Resynchronise to the last sync token on the line (a stray '$' starts a fresh frame).
		start = line.rfind(SYNC)
		if start < 0:
			return None
		delim = line.find(DELIM, start)
		if delim < 0:
			return None

		encoded = line[start + 1:delim]
		got_cksum = line[delim + 1:].strip()
		try:
			payload = base64.b64decode(encoded, validate=True)
		except Exception:
			return None
		if got_cksum.lower() != _checksum(payload):
			return None
		return payload


#: Inter-frame idle gap delimiting dgstream datagrams, in seconds (matches the firmware's
#: CONFIG_SERVICE_PROTO_DGSTREAM_RX_TIMEOUT_MS default).
DEFAULT_FRAME_GAP = 0.02


class DgstreamFramer:
	"""Reads and writes raw datagrams delimited by an idle gap over a :class:`~pynbus2.transport.stream.Stream`."""

	def __init__(self, stream, frame_gap=DEFAULT_FRAME_GAP, read_slice=0.05):
		self._stream = stream
		self._frame_gap = frame_gap
		self._read_slice = read_slice

	def write_datagram(self, data):
		"""Write one datagram as a single burst; the idle gap afterwards terminates it."""
		self._stream.write(data)

	def read_datagram(self, timeout):
		"""Read one datagram, or return ``None`` if none starts within ``timeout`` seconds.

		Once the first byte arrives the frame is read to its end (the medium idling for the frame gap)
		regardless of ``timeout``.
		"""
		deadline = (time.monotonic() + timeout) if timeout is not None else None
		frame = bytearray()
		while True:
			if frame:
				chunk = self._stream.read_some(self._frame_gap)
				if not chunk:
					return bytes(frame)
				frame += chunk
				continue

			slice_timeout = self._read_slice
			if deadline is not None:
				remaining = deadline - time.monotonic()
				if remaining <= 0:
					return None
				slice_timeout = min(slice_timeout, remaining)

			frame += self._stream.read_some(slice_timeout)
