# SPDX-License-Identifier: GPL-3.0-or-later
#
# dgtext and dgstream framing tests
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

from pynbus2.transport.framing import DgtextFramer, DgstreamFramer


def test_dgtext_roundtrip(loop_stream):
	framer = DgtextFramer(loop_stream)
	framer.write_datagram(b'\x00\x01datagram')
	line = bytes(loop_stream.written)
	assert line.startswith(b'$') and line.endswith(b'\n')

	# Noise before the frame and a split delivery are both tolerated.
	loop_stream.feed(b'garbage' + line[:5])
	loop_stream.feed(line[5:])
	assert framer.read_datagram(0.5) == b'\x00\x01datagram'


def test_dgtext_bad_checksum_is_skipped(loop_stream):
	framer = DgtextFramer(loop_stream)
	framer.write_datagram(b'first')
	bad = bytes(loop_stream.written).replace(b'*', b'*0', 1)
	loop_stream.written.clear()
	framer.write_datagram(b'second')
	loop_stream.feed(bad + bytes(loop_stream.written))
	assert framer.read_datagram(0.5) == b'second'


def test_dgtext_timeout(loop_stream):
	assert DgtextFramer(loop_stream).read_datagram(0.05) is None


def test_dgstream_gap_delimits_frames(loop_stream):
	framer = DgstreamFramer(loop_stream)
	framer.write_datagram(b'raw')
	assert bytes(loop_stream.written) == b'raw'

	loop_stream.feed(b'one ')
	loop_stream.feed(b'frame')
	assert framer.read_datagram(0.5) == b'one frame'
	assert framer.read_datagram(0.05) is None
