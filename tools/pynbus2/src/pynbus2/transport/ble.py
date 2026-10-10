# SPDX-License-Identifier: GPL-3.0-or-later
#
# Bonded BLE peripheral lookup shared by the BLE transports
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Finding and connecting the bonded BLE peripheral a ``dgble``/``rible`` URI names (SimplePyBLE, sync API).

Only **bonded** peripherals are used: the firmware requires authenticated pairing, so the device must be
paired and trusted out of band first (e.g. with ``bluetoothctl``). A bonded peripheral can be connected
over its stored bond without scanning. Scanning is avoided on purpose, as BlueZ does not re-report an
already-known device to a fresh scan.
"""

import logging
from urllib.parse import urlencode

from ..errors import NbusError, UriError

logger = logging.getLogger(__name__)


def import_simplepyble(scheme):
	try:
		import simplepyble
	except ImportError as e:
		raise ImportError(
			'the %s transport requires SimplePyBLE; install it with "pip install simplepyble" '
			'or "pip install pynbus2[ble]"' % scheme) from e
	return simplepyble


def _normalise_mac(text):
	"""Strip separators and case so MAC addresses compare regardless of formatting."""
	return text.lower().replace(':', '').replace('-', '')


def device_matches(target, peripheral):
	"""Whether ``target`` (a name or a MAC in any separator style) identifies this peripheral."""
	name = peripheral.identifier()
	address = peripheral.address()
	if name and target.lower() == name.lower():
		return True
	if address and _normalise_mac(target) == _normalise_mac(address):
		return True
	return False


def describe(peripheral):
	return '%s [%s]' % (peripheral.identifier() or '(no name)', peripheral.address())


def pick_adapter(simplepyble, wanted):
	"""Select the Bluetooth adapter, by identifier or address when ``wanted`` is given."""
	adapters = simplepyble.Adapter.get_adapters()
	if not adapters:
		raise NbusError('no Bluetooth adapter available')
	if wanted is None:
		return adapters[0]
	want = wanted.lower()
	for adapter in adapters:
		if want in (adapter.identifier().lower(), adapter.address().lower()):
			return adapter
	raise NbusError('no Bluetooth adapter matching %r (available: %s)'
	                % (wanted, ', '.join(adapter.identifier() for adapter in adapters)))


def bonded_peripherals(uri):
	"""The peripherals bonded with the adapter the URI selects (``adapter`` query parameter)."""
	simplepyble = import_simplepyble(uri.scheme)
	adapter = pick_adapter(simplepyble, uri.get('adapter'))
	paired = adapter.get_paired_peripherals()
	logger.info('bonded device(s) on adapter %s: %s', adapter.identifier(),
	            ', '.join(describe(p) for p in paired) or 'none')
	return paired


def connect_bonded(uri):
	"""Look up the bonded peripheral named by the URI host and connect it."""
	if not uri.host:
		raise UriError('the %s URI must name a device by name or MAC, e.g. %s://AA:BB:CC:DD:EE:FF/...'
		               % (uri.scheme, uri.scheme))

	paired = bonded_peripherals(uri)
	peripheral = next((p for p in paired if device_matches(uri.host, p)), None)
	if peripheral is None:
		raise NbusError('no bonded BLE device matching %r; pair and trust the device first '
		                '(e.g. with bluetoothctl), then retry. bonded device(s): %s'
		                % (uri.host, ', '.join(describe(p) for p in paired) or 'none'))

	logger.info('connecting to %s', describe(peripheral))
	peripheral.connect()
	logger.info('connected to %s', describe(peripheral))
	return peripheral


def device_uris(uri):
	"""Probe step listing the bonded devices as ``<scheme>://<mac>`` URIs, keeping the URI's query.

	The device name, when it has one, is added as the ``name`` query parameter, for display only.
	"""
	params = [(k, v) for k, values in uri.query.items() if k != 'name' for v in values]
	uris = []
	for p in bonded_peripherals(uri):
		query = params + ([('name', p.identifier())] if p.identifier() else [])
		uris.append('%s://%s%s' % (uri.scheme, p.address(), '?' + urlencode(query) if query else ''))
	return uris


def disconnect(peripheral):
	if peripheral is not None and peripheral.is_connected():
		logger.info('disconnecting from %s', describe(peripheral))
		peripheral.disconnect()
