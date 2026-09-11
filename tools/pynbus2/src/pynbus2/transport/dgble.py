# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 datagram tunnel over a BLE GATT service (proto-dgble)
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""The ``dgble`` transport: nbus2 datagrams tunnelled over a BLE GATT service.

This talks to the firmware ``services/proto-dgble`` service, which exposes one primary GATT service
per nbus2 service ID and one readable/writable/notifiable characteristic per endpoint. A datagram to
an endpoint is a write to its characteristic; a datagram from the device is a notification on it. Like
the UDP/IPv6 bridge, the payload is carried verbatim - no nbus2 wire crypto is done here.

All UUIDs are 128 bit, built from the shared vendor prefix ``a3def8c0-acdd-a602-e3a1-78`` (most
significant octet first), a one byte attribute group selector and a 32 bit tail::

	service		a3def8c0-acdd-a602-e3a1-7890<ssssssss>	 0x90 + the 32 bit service ID
	endpoint N	a3def8c0-acdd-a602-e3a1-7892<0000nnnn>	 0x92 + the 16 bit endpoint number

URI form::

	dgble://<device-name-or-mac>/<sid>/<ep>[?adapter=<name-or-mac>]

	dgble://AA:BB:CC:DD:EE:FF/00010002/3	 connect by MAC to sid 0x00010002, endpoint 3
	dgble://nwdaq-hh1/00010002/3			 connect by bonded device name

The device (name or MAC) occupies the host and the service ID plus endpoint follow in the path. Only
**bonded** peripherals are used: the firmware requires authenticated pairing, so the device must be
paired and trusted out of band first (e.g. with ``bluetoothctl``). :func:`from_uri` looks the device up
among the adapter's paired peripherals, connects over the stored bond, enumerates its GATT services and
checks that the selected service ID (SID) is present, raising if it is not. No scanning is done - it is
avoided on purpose, as BlueZ does not re-report an already-known device to a fresh scan. This transport
uses the synchronous SimplePyBLE API only.
"""

import logging
import queue

from .base import Transport, SocketBackend
from ..addressing import parse_id
from ..errors import NbusError, UriError

logger = logging.getLogger(__name__)

# Vendor prefix and attribute group selectors, mirroring services/proto-dgble/proto-dgble.c. The
# prefix is the first 11 octets (22 hex digits); the group byte and a 32 bit tail complete the UUID.
_UUID_PREFIX = 'a3def8c0-acdd-a602-e3a1-78'
_GROUP_SERVICE = 0x90
_GROUP_ENDPOINT = 0x92


def _make_uuid(group, tail):
	"""Build one of the service's 128 bit UUID strings from the group selector and a 32 bit tail."""
	return '%s%02x%08x' % (_UUID_PREFIX, group & 0xff, tail & 0xffffffff)


def _sid_to_int(sid):
	"""Normalise a service ID (hex string, int or 4 bytes) to a 32 bit int for the UUID tail."""
	return int.from_bytes(parse_id(sid), 'big')


def _parse_ep(value):
	"""Normalise a proto-dgble endpoint number to an int in the 16 bit range."""
	ep = int(str(value), 0)
	if not 0 <= ep <= 0xffff:
		raise ValueError('a dgble endpoint must fit into 16 bits')
	return ep


def _normalise_mac(text):
	"""Strip separators and case so MAC addresses compare regardless of formatting."""
	return text.lower().replace(':', '').replace('-', '')


def _device_matches(target, peripheral):
	"""Whether ``target`` (a name or a MAC in any separator style) identifies this peripheral."""
	name = peripheral.identifier()
	address = peripheral.address()
	if name and target.lower() == name.lower():
		return True
	if address and _normalise_mac(target) == _normalise_mac(address):
		return True
	return False


class DgbleTransport(Transport):

	def __init__(self, peripheral, service_uuids):
		self._peripheral = peripheral
		# Lowercased UUIDs of every GATT service enumerated on the connected device.
		self._service_uuids = service_uuids

	@classmethod
	def from_uri(cls, uri):
		try:
			import simplepyble
		except ImportError as e:
			raise ImportError(
				'the dgble transport requires SimplePyBLE; install it with "pip install simplepyble" '
				'or "pip install pynbus2[ble]"') from e

		if not uri.host:
			raise UriError('the dgble URI must name a device by name or MAC, '
						   'e.g. dgble://AA:BB:CC:DD:EE:FF/<sid>/<ep>')
		if uri.dst_sid is None:
			raise UriError('the dgble URI must carry a service ID, e.g. dgble://<device>/<sid>/<ep>')

		adapter = cls._pick_adapter(simplepyble, uri.get('adapter'))

		# Only bonded peripherals are used: the device requires authenticated pairing, and a bonded
		# peripheral can be connected over its stored bond without scanning. Scanning is avoided on
		# purpose, as BlueZ does not re-report an already-known device to a fresh scan.
		paired = adapter.get_paired_peripherals()
		listing = ', '.join('%s [%s]' % (p.identifier() or '(no name)', p.address()) for p in paired)
		logger.info('bonded device(s) on adapter %s: %s', adapter.identifier(), listing or 'none')

		peripheral = next((p for p in paired if _device_matches(uri.host, p)), None)
		if peripheral is None:
			raise NbusError('no bonded BLE device matching %r; pair and trust the device first '
							'(e.g. with bluetoothctl), then retry. bonded device(s): %s'
							% (uri.host, listing or 'none'))

		logger.info('connecting to %s [%s]', peripheral.identifier() or '(no name)', peripheral.address())
		peripheral.connect()
		logger.info('connected to %s [%s]', peripheral.identifier() or '(no name)', peripheral.address())

		# The ATT MTU exchange may only be started by the client, and the peripheral's notifications are
		# capped at MTU minus 3 bytes: a link left at the 23 byte default can only push 20 byte
		# notifications, which is what makes larger datagrams fail. Connecting triggers BlueZ to negotiate
		# the MTU; read it back and log the value (and the resulting notification ceiling) so a too-small
		# MTU is visible when a transfer misbehaves.
		try:
			mtu = peripheral.mtu()
			logger.info('negotiated ATT MTU with %s: %d bytes (notification payload up to %d)',
						uri.host, mtu, max(mtu - 3, 0))
		except Exception as e:
			logger.warning('could not read the negotiated ATT MTU: %s', e)

		service_uuids = {service.uuid().lower() for service in peripheral.services()}

		# Confirm the selected service ID (SID) advertises its GATT service on the connected device.
		sid_uuid = _make_uuid(_GROUP_SERVICE, _sid_to_int(uri.dst_sid))
		if sid_uuid not in service_uuids:
			logger.info('disconnecting from %s: service %s not present', uri.host, sid_uuid)
			peripheral.disconnect()
			raise NbusError('device %r does not expose the dgble service %s (SID %s)'
							% (uri.host, sid_uuid, uri.dst_sid))

		transport = cls(peripheral, service_uuids)
		# The path always carries the default destination (both parts are mandatory for dgble).
		transport.default_dst = (uri.dst_sid, uri.dst_ep)
		return transport

	@staticmethod
	def _pick_adapter(simplepyble, wanted):
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

	def open_socket(self):
		return DgbleSocket(self._peripheral, self._service_uuids)

	def close(self):
		if self._peripheral is not None and self._peripheral.is_connected():
			logger.info('disconnecting from %s [%s]',
						self._peripheral.identifier() or '(no name)', self._peripheral.address())
			self._peripheral.disconnect()


class DgbleSocket(SocketBackend):
	"""A single endpoint channel: a write is an outbound datagram, a notification an inbound one."""

	# A retransmit is the real hazard here: when a still-in-flight reply is treated as lost, the resend
	# makes the device answer twice and the surplus reply desyncs the following request (a read then
	# returns the previous block). Because recv() is event-driven - it returns the instant the reply
	# arrives - a generous ceiling costs nothing in the common case; it only has to exceed the device's
	# worst-case turnaround (notably the first request after a close, and flash erase/write latency) so
	# a retransmit fires only on a genuine loss, not on a slow answer.
	request_timeout = 1.00

	def __init__(self, peripheral, service_uuids):
		self._peripheral = peripheral
		self._service_uuids = service_uuids

		self._service_uuid = None
		self._char_uuid = None
		self._queue = queue.Queue()
		self._subscribed = False

	def bind(self, id, ep):
		# The BLE tunnel carries datagrams verbatim per endpoint; there is no host-side source address.
		pass

	def connect(self, id, ep):
		service_uuid = _make_uuid(_GROUP_SERVICE, _sid_to_int(id))
		if service_uuid not in self._service_uuids:
			raise NbusError('the device does not expose the dgble service %s' % service_uuid)
		char_uuid = _make_uuid(_GROUP_ENDPOINT, _parse_ep(ep))

		self._service_uuid = service_uuid
		self._char_uuid = char_uuid
		# Subscribe to endpoint notifications so inbound datagrams are queued for recv().
		logger.info('subscribing to endpoint characteristic %s (service %s)', char_uuid, service_uuid)
		self._peripheral.notify(service_uuid, char_uuid, self._on_notify)
		self._subscribed = True

	def _on_notify(self, data):
		self._queue.put(bytes(data))

	def flush(self):
		# Drop replies still queued from an earlier request (e.g. a duplicate produced by a retransmit)
		# so the next request is matched against a reply that actually answers it, not a stale one.
		dropped = 0
		try:
			while True:
				self._queue.get_nowait()
				dropped += 1
		except queue.Empty:
			pass
		return dropped

	def send(self, data):
		if self._char_uuid is None:
			raise OSError('socket is not connected to a destination')
		# The endpoint characteristic is write-with-response (matching the firmware profile).
		self._peripheral.write_request(self._service_uuid, self._char_uuid, bytes(data))

	def recv(self, timeout):
		if self._char_uuid is None:
			raise OSError('socket is not connected to a destination')
		try:
			return self._queue.get(timeout=timeout) if timeout is not None else self._queue.get()
		except queue.Empty:
			return None

	def close(self):
		if self._subscribed:
			logger.info('unsubscribing from endpoint characteristic %s', self._char_uuid)
			try:
				self._peripheral.unsubscribe(self._service_uuid, self._char_uuid)
			except Exception as e:
				logger.warning('unsubscribe from %s failed: %s', self._char_uuid, e)
			self._subscribed = False
