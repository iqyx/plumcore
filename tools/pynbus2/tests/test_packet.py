# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 packet codec tests
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

from pynbus2 import packet
from pynbus2.addressing import parse_id
from pynbus2.blake2s_siv import derive_keys

KE, KM = derive_keys('abcd')
DST, SRC = parse_id('00010002'), parse_id('00000001')


def test_roundtrip():
	wire = packet.encode(b'hello', DST, 3, SRC, 0, 7, KE, KM)
	pkt = packet.decode(wire, KE, KM)
	assert pkt == packet.Packet(DST, 3, SRC, 0, False, b'hello')


def test_multicast_flag():
	wire = packet.encode(b'adv', DST, 0, SRC, 2, 0, KE, KM, multicast=True)
	assert packet.decode(wire, KE, KM).multicast


def test_tampered_packet_is_rejected():
	wire = bytearray(packet.encode(b'hello', DST, 3, SRC, 0, 7, KE, KM))
	wire[-1] ^= 1
	assert packet.decode(bytes(wire), KE, KM) is None


def test_wrong_key_is_rejected():
	wire = packet.encode(b'hello', DST, 3, SRC, 0, 7, KE, KM)
	ke, km = derive_keys('other')
	assert packet.decode(wire, ke, km) is None
