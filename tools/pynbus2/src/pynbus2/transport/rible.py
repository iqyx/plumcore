# SPDX-License-Identifier: GPL-3.0-or-later
#
# RemoteInterface sessions over a BLE GATT service (remote-interface-ble)
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""The ``rible`` transport: a session on a RemoteInterface exported over BLE.

This talks to the firmware ``services/remote-interface-ble`` service. It exposes a single primary GATT
service with two readable/writable/notifiable characteristics. All UUIDs are the first 15 bytes of
``sha256("RemoteInterfaceBle")`` followed by an attribute selector::

	service		e146becd-ab61-d423-6586-22a4b435e700
	control		e146becd-ab61-d423-6586-22a4b435e701	one CBOR request per write, one response per notification
	data		e146becd-ab61-d423-6586-22a4b435e702	1 byte header + message fragment

Control requests are CBOR maps carrying the command in ``"c"`` (``walk``, ``desc``, ``open``, ``close``),
responses carry only the result fields or ``"err"``. An opened session is bound to a data channel. Every data
value starts with a header byte (bit 7 START, bit 6 END, bits 5..0 channel), a message is split into
consecutive fragments of one channel. An empty message from the device means the session was closed by the
interface.

URI form::

	rible://<device-name-or-mac>[/<path>/<to>/<interface>][?adapter=<name-or-mac>]

	rible://nwdaq-hh1/conf			interface advertised as "conf"
	rible://nwdaq-hh1/card1/flash	interface advertised as "card1 flash"
	rible://nwdaq-hh1				discover the interfaces

The path components are joined with spaces into the name the interface was advertised with in the
firmware service locator. Without a path the interfaces are walked and logged, the root interface ``""``
becomes the default destination if the device has one. Only **bonded** peripherals are used, the same as the ``dgble`` transport.
"""

import logging
import queue
import threading

from .base import Transport, SocketBackend
from .dgble import DgbleTransport, _device_matches
from ..errors import NbusError, UriError

logger = logging.getLogger(__name__)

_UUID_PREFIX = 'e146becd-ab61-d423-6586-22a4b435e7'
_SERVICE_UUID = _UUID_PREFIX + '00'
_CONTROL_UUID = _UUID_PREFIX + '01'
_DATA_UUID = _UUID_PREFIX + '02'

_HDR_START = 0x80
_HDR_END = 0x40
_HDR_CH_MASK = 0x3f

# Largest characteristic value carried by a single write or notification (ATT MTU 247 minus 3).
_VALUE_MAX = 244

_CONTROL_TIMEOUT = 2.0


class RibleTransport(Transport):

	def __init__(self, peripheral):
		self._peripheral = peripheral
		self._cbor = None

		self._control_lock = threading.Lock()
		self._control_queue = queue.Queue()

		# Inbound messages per open data channel, and the reassembly of the message in progress.
		self._channels = {}
		self._rx_ch = None
		self._rx_buf = bytearray()

	@classmethod
	def from_uri(cls, uri):
		try:
			import simplepyble
		except ImportError as e:
			raise ImportError(
				'the rible transport requires SimplePyBLE; install it with "pip install simplepyble" '
				'or "pip install pynbus2[ble]"') from e
		try:
			import cbor2
		except ImportError as e:
			raise ImportError('the rible transport requires cbor2; install it with "pip install cbor2"') from e

		if not uri.host:
			raise UriError('the rible URI must name a device by name or MAC, '
						   'e.g. rible://AA:BB:CC:DD:EE:FF/<path>/<to>/<interface>')

		adapter = DgbleTransport._pick_adapter(simplepyble, uri.get('adapter'))

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

		transport = cls(peripheral)
		transport._cbor = cbor2
		try:
			transport._setup()
			# The interface name is carried as the default destination, the endpoint is not used.
			if uri.path_segments:
				name = ' '.join(uri.path_segments)
				transport._log_desc(name, transport.control({'c': 'desc', 'n': name}))
				transport.default_dst = (name, 0)
			else:
				interfaces = transport.walk()
				logger.info('%d interface(s) found', len(interfaces))
				for name, desc in interfaces:
					transport._log_desc(name, desc)
					if name == '':
						transport.default_dst = (name, 0)
		except Exception:
			transport.close()
			raise
		return transport

	def _setup(self):
		try:
			mtu = self._peripheral.mtu()
			logger.info('negotiated ATT MTU: %d bytes (fragment payload up to %d)', mtu, self._fragment_max())
		except Exception as e:
			logger.warning('could not read the negotiated ATT MTU: %s', e)

		service_uuids = {service.uuid().lower() for service in self._peripheral.services()}
		if _SERVICE_UUID not in service_uuids:
			raise NbusError('the device does not expose the remote-interface-ble service %s' % _SERVICE_UUID)

		logger.info('subscribing to the control and data characteristics')
		self._peripheral.notify(_SERVICE_UUID, _CONTROL_UUID, self._on_control)
		self._peripheral.notify(_SERVICE_UUID, _DATA_UUID, self._on_data)

	@staticmethod
	def _log_desc(name, desc):
		logger.info('interface %r: protocol %s, version %s, mtu %s, max sessions %s', name, desc.get('p'),
					desc.get('pv', '(none)'), desc.get('mtu'), desc.get('ms'))

	def walk(self):
		"""Walk all interfaces exported by the device and return a list of (name, desc) tuples. The desc is the
		response map of the desc command (p, pv, mtu, ms)."""
		interfaces = []
		name = None
		while True:
			name = self.control({'c': 'walk', 'n': name}).get('n')
			if name is None:
				return interfaces
			interfaces.append((name, self.control({'c': 'desc', 'n': name})))

	def _fragment_max(self):
		try:
			mtu = self._peripheral.mtu()
		except Exception:
			mtu = 23
		return min(mtu - 3, _VALUE_MAX) - 1

	def control(self, req):
		"""Send a control request and return the response map. Raises NbusError on a timeout or an error
		response. Requests are not retransmitted, open and close are not idempotent."""
		with self._control_lock:
			try:
				while True:
					self._control_queue.get_nowait()
			except queue.Empty:
				pass
			logger.debug('control request: %s', req)
			self._peripheral.write_request(_SERVICE_UUID, _CONTROL_UUID, self._cbor.dumps(req))
			try:
				resp = self._cbor.loads(self._control_queue.get(timeout=_CONTROL_TIMEOUT))
			except queue.Empty:
				raise NbusError('no response to the control request %s' % req)
			logger.debug('control response: %s', resp)
		if not isinstance(resp, dict):
			raise NbusError('malformed control response %r' % resp)
		if 'err' in resp:
			raise NbusError('control request %s failed: %s' % (req, resp['err']))
		return resp

	def _on_control(self, data):
		self._control_queue.put(bytes(data))

	def _on_data(self, data):
		data = bytes(data)
		if not data:
			return
		ch = data[0] & _HDR_CH_MASK
		if data[0] & _HDR_START:
			self._rx_ch = ch
			self._rx_buf = bytearray()
		elif self._rx_ch != ch:
			logger.debug('ch %d: dropping a fragment not continuing a message', ch)
			return
		self._rx_buf += data[1:]
		if not data[0] & _HDR_END:
			return
		self._rx_ch = None
		q = self._channels.get(ch)
		if q is None:
			logger.debug('ch %d: dropping a message for a channel not open', ch)
			return
		# An empty message is the closing indication, it is queued as None.
		q.put(bytes(self._rx_buf) if self._rx_buf else None)

	def send_message(self, ch, data):
		"""Send a message on a data channel, split into fragments."""
		frag_max = self._fragment_max()
		pos = 0
		while True:
			chunk = data[pos:pos + frag_max]
			hdr = ch
			if pos == 0:
				hdr |= _HDR_START
			if pos + len(chunk) >= len(data):
				hdr |= _HDR_END
			# The data characteristic is write-with-response (matching the firmware profile).
			self._peripheral.write_request(_SERVICE_UUID, _DATA_UUID, bytes([hdr]) + chunk)
			pos += len(chunk)
			if pos >= len(data):
				break

	def open_channel(self, name):
		resp = self.control({'c': 'open', 'n': name})
		ch = resp['ch']
		self._channels[ch] = queue.Queue()
		logger.info('opened interface %r on channel %d', name, ch)
		return ch, self._channels[ch]

	def close_channel(self, ch):
		self._channels.pop(ch, None)
		self.control({'c': 'close', 'ch': ch})
		logger.info('closed channel %d', ch)

	def open_socket(self):
		return RibleSocket(self)

	def close(self):
		if self._peripheral is not None and self._peripheral.is_connected():
			logger.info('disconnecting from %s [%s]',
						self._peripheral.identifier() or '(no name)', self._peripheral.address())
			self._peripheral.disconnect()


class RibleSocket(SocketBackend):
	"""A session on a single RemoteInterface, bound to a data channel."""

	# The same reasoning as for dgble applies: recv() returns as soon as the reply arrives, the timeout
	# only has to exceed the device's worst-case turnaround so a retransmit fires on a genuine loss.
	request_timeout = 1.00

	def __init__(self, transport):
		self._transport = transport
		self._ch = None
		self._queue = None
		self._closed = False

	def bind(self, id, ep):
		pass

	def connect(self, id, ep):
		# The destination is the interface name, the endpoint is not used.
		self._ch, self._queue = self._transport.open_channel(id)

	def flush(self):
		dropped = 0
		try:
			while True:
				if self._queue.get_nowait() is None:
					self._closed = True
				dropped += 1
		except queue.Empty:
			pass
		return dropped

	def send(self, data):
		if self._ch is None:
			raise OSError('socket is not connected to a destination')
		if self._closed:
			raise NbusError('the session on channel %d was closed by the device' % self._ch)
		self._transport.send_message(self._ch, bytes(data))

	def recv(self, timeout):
		if self._ch is None:
			raise OSError('socket is not connected to a destination')
		if self._closed:
			return None
		try:
			msg = self._queue.get(timeout=timeout) if timeout is not None else self._queue.get()
		except queue.Empty:
			return None
		if msg is None:
			logger.warning('the session on channel %d was closed by the device', self._ch)
			self._closed = True
		return msg

	def close(self):
		if self._ch is None:
			return
		# The channel stays reserved on the device until it is closed explicitly, even when the session
		# was closed by the device.
		try:
			self._transport.close_channel(self._ch)
		except Exception as e:
			logger.warning('closing channel %d failed: %s', self._ch, e)
		self._ch = None
