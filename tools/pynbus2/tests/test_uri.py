# SPDX-License-Identifier: GPL-3.0-or-later
#
# URI parsing and discovery entry point tests
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import pytest

import pynbus2
from pynbus2.uri import parse_uri, split_serial_path


@pytest.mark.parametrize('uri, expected', [
	('dgtext+serial:///dev/ttyUSB0', ('/dev/ttyUSB0', None)),
	('dgtext+serial:///dev/ttyUSB0/00010002/3', ('/dev/ttyUSB0', ('00010002', '3'))),
	('dgtext+serial:///dev/serial/by-id/usb-x', ('/dev/serial/by-id/usb-x', None)),
	('dgtext+serial://com3/00010002/3', ('com3', ('00010002', '3'))),
])
def test_split_serial_path(uri, expected):
	assert split_serial_path(parse_uri(uri)) == expected


def test_mac_address_host():
	uri = parse_uri('dgble://AA:BB:CC:DD:EE:FF/00010002/3')
	assert (uri.host, uri.dst_sid, uri.dst_ep) == ('AA:BB:CC:DD:EE:FF', '00010002', '3')


def test_probe_lists_schemes():
	assert 'swd://' in pynbus2.probe('')
	assert 'dgstream+serial://' in pynbus2.probe('')


def test_swd_probe_lists_the_flash_interface():
	assert pynbus2.probe('swd://probe1?target=t') == ['swd://probe1/flash?target=t&p=flash&pv=1.0.0']


def test_unknown_scheme():
	with pytest.raises(pynbus2.UriError):
		pynbus2.connect('nope://')
