# SPDX-License-Identifier: GPL-3.0-or-later
#
# Virtual flash service protocol tests
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import pytest

from pynbus2.services import flash

cbor2 = pytest.importorskip('cbor2')

SIZE, SECTOR, PAGE = 4096, 1024, 256


class RamVolume(flash.FlashVolume):

	name = 'ram'

	def __init__(self):
		self.mem = bytearray(b'\xff' * SIZE)
		self.closed = 0

	def sizes(self):
		return [(SIZE, flash.OPS_ERASE), (SECTOR, flash.OPS_ERASE), (PAGE, flash.OPS_READ | flash.OPS_WRITE)]

	def read(self, addr, length):
		return bytes(self.mem[addr:addr + length])

	def erase(self, addr, length):
		if addr % SECTOR or length % SECTOR:
			raise flash.FlashError('unaligned')
		self.mem[addr:addr + length] = b'\xff' * length

	def write(self, addr, data):
		self.mem[addr:addr + len(data)] = data

	def close(self):
		self.closed += 1


@pytest.fixture
def service():
	resets = []
	svc = flash.FlashService([RamVolume()], reset=lambda: resets.append(True))
	svc.resets = resets
	return svc


def call(service, req):
	return cbor2.loads(service.handle(cbor2.dumps(req)))


def test_list_and_info(service):
	assert call(service, {'c': 'list'}) == {'d': [{'n': 'ram'}]}
	assert call(service, {'c': 'info', 'n': 'ram'})['sizes'][1] == {'s': SECTOR, 'ops': flash.OPS_ERASE}
	assert call(service, {'c': 'info', 'n': 'nope'}) == {'err': 'device not found'}


def test_session_erase_write_read(service):
	sid = call(service, {'c': 'open', 'n': 'ram'})['sid']
	assert call(service, {'c': 'erase', 'sid': sid, 'addr': 0, 'len': SECTOR}) == {'ret': 'ok'}
	assert call(service, {'c': 'write', 'sid': sid, 'addr': 128, 'len': 3, 'd': b'abc'}) == {'ret': 'ok'}
	assert call(service, {'c': 'read', 'sid': sid, 'addr': 127, 'len': 5}) == {'addr': 127, 'd': b'\xffabc\xff'}
	assert call(service, {'c': 'close'}) == {'sid': sid}
	assert call(service, {'c': 'close'}) == {'err': 'no session opened'}


def test_errors(service):
	assert call(service, {'c': 'read', 'sid': 1, 'addr': 0, 'len': 1}) == {'err': 'session error'}
	sid = call(service, {'c': 'open', 'n': 'ram'})['sid']
	assert call(service, {'c': 'read', 'sid': sid + 1, 'addr': 0, 'len': 1}) == {'err': 'session error'}
	assert call(service, {'c': 'read', 'sid': sid, 'addr': 0, 'len': flash.BLOCK_LEN + 1}) == {'err': 'bad arguments'}
	assert call(service, {'c': 'read', 'sid': sid, 'addr': SIZE - 1, 'len': 2}) == {'err': 'bad arguments'}
	assert call(service, {'c': 'write', 'sid': sid, 'addr': 0, 'len': 2, 'd': b'a'}) == {'err': 'bad arguments'}
	assert call(service, {'c': 'erase', 'sid': sid, 'addr': 1, 'len': SECTOR}) == {'err': 'erasing failed'}


def test_unknown_and_malformed_requests(service):
	assert call(service, {'c': 'bogus'}) == {}
	assert service.handle(cbor2.dumps([1, 2])) is None
	assert service.handle(b'\xff\xff') is None


def test_reset(service):
	call(service, {'c': 'open', 'n': 'ram'})
	assert call(service, {'c': 'reset'}) == {'ret': 'ok'}
	assert service.resets == [True]
