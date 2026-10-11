# SPDX-License-Identifier: GPL-3.0-or-later
#
# SWD flash volume tests against a fake pyocd target
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import pytest

pytest.importorskip('pyocd')

from pynbus2.services.flash import FlashError
from pynbus2.transport.swd import SwdFlashVolume


class FakeFlash:

	def __init__(self, mem):
		self.mem = mem
		self.ops = []

	def init(self, operation):
		self.ops.append(('init', operation.name))

	def erase_sector(self, address):
		self.ops.append(('erase', address))

	def program_page(self, address, data):
		self.ops.append(('program', address, len(data)))
		self.mem[address] = bytes(data)

	def cleanup(self):
		self.ops.append(('cleanup',))


class FakeRegion:

	name = 'flash'
	start = 0x08000000
	length = 0x10000
	sector_size = 0x800
	page_size = 0x100
	phrase_size = 8
	erased_byte_value = 0xff

	def __init__(self):
		self.mem = {}
		self.flash = FakeFlash(self.mem)


class FakeTarget:

	def read_memory_block8(self, addr, length):
		return [addr & 0xff] * length


def test_sizes_and_read():
	volume = SwdFlashVolume(FakeTarget(), FakeRegion(), 'flash0')
	assert [size for size, _ops in volume.sizes()] == [0x10000, 0x800, 0x100]
	assert volume.read(0x10, 2) == b'\x10\x10'


def test_erase_covers_whole_sectors():
	region = FakeRegion()
	SwdFlashVolume(FakeTarget(), region, 'flash0').erase(0x800, 0x1000)
	assert region.flash.ops == [('init', 'ERASE'), ('erase', 0x08000800), ('erase', 0x08001000)]
	with pytest.raises(FlashError):
		SwdFlashVolume(FakeTarget(), region, 'flash0').erase(0x10, 0x800)


def test_write_splits_at_page_boundary_and_pads_to_phrase():
	region = FakeRegion()
	SwdFlashVolume(FakeTarget(), region, 'flash0').write(0xf8, b'x' * 13)
	assert region.flash.ops == [('init', 'PROGRAM'), ('program', 0x080000f8, 8), ('program', 0x08000100, 8)]
	assert region.mem[0x08000100] == b'xxxxx\xff\xff\xff'
	with pytest.raises(FlashError):
		SwdFlashVolume(FakeTarget(), region, 'flash0').write(0x4, b'x' * 8)


def test_volumes_skip_aliases_and_are_numbered_by_address():
	import types
	from pynbus2.transport.swd import SwdTransport

	ns, bank2, secure = FakeRegion(), FakeRegion(), FakeRegion()
	ns.is_flash = bank2.is_flash = secure.is_flash = True
	bank2.start = 0x08100000
	secure.start, secure.alias = 0x0c000000, ns
	ram = types.SimpleNamespace(is_flash=False)
	target = types.SimpleNamespace(memory_map=types.SimpleNamespace(regions=[secure, bank2, ram, ns]))
	transport = SwdTransport(types.SimpleNamespace(target=target))
	assert [(v.name, v._region.start) for v in transport._volumes] == [('flash0', 0x08000000), ('flash1', 0x08100000)]


def test_flash_interface_takes_whole_algorithm_pages():
	import types
	from pynbus2.transport import swd

	region = FakeRegion()
	region.is_flash = True
	target = types.SimpleNamespace(memory_map=types.SimpleNamespace(regions=[region]))
	transport = swd.SwdTransport(types.SimpleNamespace(target=target))
	assert transport._interfaces['flash'].block_len >= region.page_size
	[(name, desc)] = transport.walk()
	assert desc['mtu'] > transport._interfaces['flash'].block_len
