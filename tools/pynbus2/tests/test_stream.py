# SPDX-License-Identifier: GPL-3.0-or-later
#
# Stream transport dispatch tests
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import threading

import pytest

from pynbus2 import packet
from pynbus2.addressing import parse_id
from pynbus2.advertisement import ADV_MULTICAST_ID, ADV_MULTICAST_EP
from pynbus2.blake2s_siv import derive_keys
from pynbus2.nbus import NbusSocket
from pynbus2.transport.framing import DgtextFramer
from pynbus2.transport.stream import StreamTransport

cbor2 = pytest.importorskip('cbor2')

KE, KM = derive_keys('abcd')
SELF = parse_id('00000001')
DEVICE = parse_id('00010002')


class LoopTransport(StreamTransport):

	@classmethod
	def from_uri(cls, uri):
		raise NotImplementedError


def _transport(stream, **kwargs):
	return LoopTransport(stream, DgtextFramer(stream), KE, KM, SELF, **kwargs)


def _feed(stream, payload, dst_id, dst_ep, src_id, src_ep, multicast=False):
	wire = packet.encode(payload, dst_id, dst_ep, src_id, src_ep, 0, KE, KM, multicast=multicast)
	DgtextFramer(stream).write_datagram(wire)
	stream.feed(bytes(stream.written))
	stream.written.clear()


def test_reply_is_dispatched_to_the_connected_socket(loop_stream):
	transport = _transport(loop_stream)
	try:
		sock = NbusSocket(transport.open_socket()).connect(DEVICE, 3)
		_feed(loop_stream, b'reply', SELF, 0, DEVICE, 3)
		assert sock.recv(1.0) == b'reply'
	finally:
		transport.close()


def test_advertisement_is_not_taken_as_a_reply(loop_stream):
	"""A service advertises from its own sid/ep, which a socket connected to it matches by source."""
	found = []
	got = threading.Event()
	transport = _transport(loop_stream, on_advertisement=lambda adv: (found.append(adv), got.set()))
	try:
		sock = NbusSocket(transport.open_socket()).connect(DEVICE, 3)
		_feed(loop_stream, cbor2.dumps({'adv': 1, 'p': 'flash', 'pv': '1.0.0'}),
		      ADV_MULTICAST_ID, ADV_MULTICAST_EP, DEVICE, 3, multicast=True)
		assert got.wait(1.0)
		assert sock.recv(0.1) is None
		assert (found[0].src_id, found[0].src_ep, found[0].protocol, found[0].protocol_version) == \
			(DEVICE, 3, 'flash', '1.0.0')
	finally:
		transport.close()
