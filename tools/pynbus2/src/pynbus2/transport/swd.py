# SPDX-License-Identifier: GPL-3.0-or-later
#
# SWD debug probe transport serving virtual services (pyocd)
#
# Copyright (c) 2025, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""The ``swd`` transport: reach a target over a debug probe instead of a running firmware.

The target is driven directly through a debug probe (SWD, via pyocd), so there is no firmware to talk
to. Instead the transport serves *virtual* interfaces on the host, implemented on top of the debug
access, and they are used exactly like the interfaces reached over the other transports. Like the
``rible`` transport they are selected by name: the path components are joined with spaces into the
interface name (``/card1/flash`` is ``"card1 flash"``).

Today the only interface is ``flash``: the target's raw flash exported through the standard flash
protocol (see :mod:`pynbus2.services.flash`). Each pyocd flash region is exported as one volume, named
``flash0``, ``flash1``, ... in address order, with addresses relative to the region start. Regions which
are only an alias of another region (e.g. the secure alias of the flash on TrustZone devices) are
skipped. ``tools/proto-nbus-flash.py`` works over
it unchanged and can be used to recover a target with a broken bootloader.

URI form::

    swd://<probe-unique-id>[/<path>/<to>/<interface>][?target=<pyocd target>&pack=<path to .pack>&frequency=<Hz>]

    swd://066DFF555187534867173540                      the flash interface
    swd://066DFF555187534867173540/flash?target=stm32g491ccux&pack=Keil.STM32G4xx_DFP.2.0.0.pack

The host is the unique ID of the probe (a case-insensitive substring match). Without a path the
``flash`` interface is selected. The query selects the pyocd target type (``target``, autodetected by
pyocd when not given), the CMSIS pack providing the target and its flash algorithm (``pack``) and the
SWD clock frequency in Hz (``frequency``).

The target is halted and reset when the flash is first erased or programmed (the flash algorithm runs
in the target RAM). Resetting it with the flash ``reset`` command or closing the transport resumes it.
"""

import logging
import queue
import threading

from .base import Transport, SocketBackend
from ..errors import NbusError, UriError
from ..services import flash as flash_service

logger = logging.getLogger(__name__)

#: The interface selected when the URI has no path.
DEFAULT_INTERFACE = 'flash'

#: Largest message an interface accepts, reported by :meth:`SwdTransport.walk` (NBUS_FLASH_RX_BUF_LEN).
_MTU = 320


def _import_pyocd():
	try:
		import pyocd
	except ImportError as e:
		raise ImportError('the swd transport requires pyocd; install it with "pip install pyocd" '
		                  'or "pip install pynbus2[swd]"') from e
	return pyocd


class SwdFlashVolume(flash_service.FlashVolume):
	"""One pyocd flash region exported as a flash volume named ``name``."""

	def __init__(self, target, region, name):
		self.name = name
		self._target = target
		self._region = region

	def sizes(self):
		region = self._region
		ops = flash_service.OPS_ERASE if region.flash is not None else 0
		page_ops = flash_service.OPS_READ
		if region.flash is not None:
			page_ops |= flash_service.OPS_WRITE
		return [
			(region.length, ops),
			(region.sector_size, ops),
			(region.page_size, page_ops),
		]

	def _flash(self, operation):
		from pyocd.flash.flash import Flash

		flash = self._region.flash
		if flash is None:
			raise flash_service.FlashError('no flash algorithm for region %r, pass a pack' % self.name)
		# Switching the algorithm between erasing and programming re-initializes it, it is loaded into
		# the target RAM only once per session.
		flash.init(Flash.Operation.ERASE if operation == 'erase' else Flash.Operation.PROGRAM)
		return flash

	def read(self, addr, length):
		try:
			return bytes(self._target.read_memory_block8(self._region.start + addr, length))
		except Exception as e:
			raise flash_service.FlashError(str(e)) from e

	def erase(self, addr, length):
		sector = self._region.sector_size
		if addr % sector or length % sector:
			raise flash_service.FlashError('erase range is not aligned to %d byte sectors' % sector)
		try:
			flash = self._flash('erase')
			for offset in range(addr, addr + length, sector):
				flash.erase_sector(self._region.start + offset)
		except flash_service.FlashError:
			raise
		except Exception as e:
			raise flash_service.FlashError(str(e)) from e

	def write(self, addr, data):
		region = self._region
		phrase = region.phrase_size
		if addr % phrase:
			raise flash_service.FlashError('write address is not aligned to %d bytes' % phrase)
		# Pad the tail to whole program phrases with the erased value, the block is erased beforehand.
		if len(data) % phrase:
			data += bytes([region.erased_byte_value]) * (phrase - len(data) % phrase)
		try:
			flash = self._flash('program')
			pos = 0
			while pos < len(data):
				# A single program call must not cross a page boundary.
				chunk = min(len(data) - pos, region.page_size - (addr + pos) % region.page_size)
				flash.program_page(region.start + addr + pos, list(data[pos:pos + chunk]))
				pos += chunk
		except flash_service.FlashError:
			raise
		except Exception as e:
			raise flash_service.FlashError(str(e)) from e

	def close(self):
		if self._region.flash is not None:
			self._region.flash.cleanup()


class SwdTransport(Transport):

	def __init__(self, session):
		self._session = session
		target = session.target
		self._lock = threading.Lock()
		regions = sorted((region for region in target.memory_map.regions
		                  if region.is_flash and getattr(region, 'alias', None) is None), key=lambda r: r.start)
		self._volumes = [SwdFlashVolume(target, region, 'flash%d' % i) for i, region in enumerate(regions)]
		self._interfaces = {
			'flash': flash_service.FlashService(self._volumes, reset=self._reset),
		}
		for volume, region in zip(self._volumes, regions):
			logger.info('flash volume %r: region %r at 0x%08x, %s', volume.name, region.name, region.start,
			            ', '.join('%d B' % size for size, _ops in volume.sizes()))

	@staticmethod
	def _options(uri):
		options = {}
		if uri.get('target'):
			options['target_override'] = uri.get('target')
		if uri.get('pack'):
			options['pack'] = uri.get('pack')
		if uri.get('frequency'):
			options['frequency'] = int(uri.get('frequency'))
		return options

	@classmethod
	def from_uri(cls, uri):
		_import_pyocd()
		from pyocd.core.helpers import ConnectHelper
		from pyocd.core.session import Session

		if not uri.host:
			raise UriError('the swd URI must name a debug probe by its unique ID, e.g. swd://<probe-id>')

		probes = ConnectHelper.get_all_connected_probes(blocking=False, unique_id=uri.host)
		if not probes:
			raise NbusError('no debug probe matching %r' % uri.host)
		if len(probes) > 1:
			raise NbusError('debug probe ID %r is ambiguous, it matches %s'
			                % (uri.host, ', '.join(probe.unique_id for probe in probes)))

		session = Session(probes[0], options=cls._options(uri))
		logger.info('opening debug probe %s', probes[0].unique_id)
		try:
			session.open()
		except Exception as e:
			session.close()
			raise NbusError('cannot open debug probe %s: %s' % (probes[0].unique_id, e)) from e

		try:
			transport = cls(session)
		except Exception:
			session.close()
			raise
		# The interface name is carried as the default destination, the endpoint is not used.
		transport.default_dst = (' '.join(uri.path_segments) or DEFAULT_INTERFACE, 0)
		return transport

	@classmethod
	def probe(cls, uri):
		"""Enumerate the debug probes, then the virtual interfaces behind one.

		Probed with just the scheme it lists the connected debug probes as ``swd://<probe-id>``. Probed
		with a probe it lists the interfaces as ``swd://<probe-id>/<path>/<to>/<interface>?p=...&pv=...``,
		without connecting the target. The query (target, pack, ...) is kept.
		"""
		query = uri.query_string()
		suffix = '?' + query if query else ''
		if not uri.host:
			_import_pyocd()
			from pyocd.core.helpers import ConnectHelper

			return ['swd://%s%s' % (probe.unique_id, suffix)
			        for probe in ConnectHelper.get_all_connected_probes(blocking=False)]

		params = 'p=%s&pv=%s' % (flash_service.PROTOCOL, flash_service.PROTOCOL_VERSION)
		return ['swd://%s/%s?%s' % (uri.host, name.replace(' ', '/'), query + '&' + params if query else params)
		        for name, _desc in cls._descriptions()]

	@staticmethod
	def _descriptions():
		"""The (name, desc) of each virtual interface, in the form the rible ``desc`` command returns."""
		return [('flash', {'p': flash_service.PROTOCOL, 'pv': flash_service.PROTOCOL_VERSION, 'mtu': _MTU, 'ms': 1})]

	def walk(self):
		"""The virtual interfaces as a list of (name, desc) tuples, the same as the rible transport."""
		return self._descriptions()

	def _reset(self):
		logger.info('resetting the target')
		for volume in self._volumes:
			volume.close()
		self._session.target.reset()

	def _interface(self, name):
		interface = self._interfaces.get(name)
		if interface is None:
			raise NbusError('no virtual interface %r (available: %s)' % (name, ', '.join(self._interfaces)))
		return interface

	def _handle(self, service, data):
		with self._lock:
			return service.handle(data)

	def open_socket(self):
		return SwdSocket(self)

	def close(self):
		if self._session is None:
			return
		with self._lock:
			for service in self._interfaces.values():
				service.close()
			for volume in self._volumes:
				try:
					volume.close()
				except Exception as e:
					logger.warning('cleaning up the flash algorithm of %r failed: %s', volume.name, e)
			logger.info('closing debug probe')
			self._session.close()
			self._session = None


class SwdSocket(SocketBackend):
	"""A session on a virtual interface: each request is handled synchronously when sent."""

	def __init__(self, transport):
		self._transport = transport
		self._service = None
		self._queue = queue.Queue()

	def bind(self, id, ep):
		pass

	def connect(self, id, ep):
		# The destination is the interface name, the endpoint is not used.
		self._service = self._transport._interface(id)

	def send(self, data):
		if self._service is None:
			raise OSError('socket is not connected to a destination')
		reply = self._transport._handle(self._service, bytes(data))
		if reply is not None:
			self._queue.put(reply)

	def recv(self, timeout):
		if self._service is None:
			raise OSError('socket is not connected to a destination')
		try:
			return self._queue.get(timeout=timeout) if timeout is not None else self._queue.get()
		except queue.Empty:
			return None

	def flush(self):
		dropped = 0
		try:
			while True:
				self._queue.get_nowait()
				dropped += 1
		except queue.Empty:
			pass
		return dropped

	def close(self):
		self._service = None
