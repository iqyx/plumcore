# SPDX-License-Identifier: GPL-3.0-or-later
#
# Host-side implementation of the nbus-flash service protocol
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""A virtual ``flash`` service speaking the firmware ``services/nbus-flash`` protocol on the host.

Transports that do not reach a running firmware (e.g. ``swd``, which drives the target through a debug
probe) can still expose the target's flash through the standard flash protocol, so the same clients
(``tools/proto-nbus-flash.py``, pydaui) work over them unchanged. :class:`FlashService` turns one
request datagram into one reply datagram, the transport carries them.

Requests and replies are CBOR maps, the command is carried in ``"c"``::

    list                              {"d": [{"n": <volume name>}, ...]}
    info   n=<volume>                 {"sizes": [{"s": <size>, "ops": <ops>}, ...]}
    open   n=<volume>                 {"sid": <session id>}
    close                             {"sid": <session id>}
    erase  sid, addr, len             {"ret": "ok"}
    write  sid, addr, len, d          {"ret": "ok"}
    read   sid, addr, len             {"addr": <addr>, "d": <data>}
    usage  sid                        {"ba": <aggregation>, "b": <bitmap>}
    reset                             {"ret": "ok"}

Errors are reported as ``{"err": <message>}`` with the same messages the firmware uses. Addresses are
relative to the start of the volume. ``sizes`` lists the volume size first, then the erase block sizes
from the largest to the smallest and the program page size last, each with the operations it supports
(``OPS_*``). ``usage`` estimates which erase blocks are in use: bit ``n / ba`` of the bitmap (LSB first in
each byte) is 0 only if all ``ba`` erase blocks it covers are erased, a block counts as erased if its first
32 bit word reads ``0xffffffff``. ``ba`` is the smallest aggregation fitting the bitmap in a block
(``BLOCK_LEN`` bytes by default); if it is missing, it is 1. Unlike the firmware, which resets without answering, ``reset`` is answered.
"""

import abc
import logging

logger = logging.getLogger(__name__)

#: Protocol name and version advertised by the firmware service (NBUS_FLASH_INTERFACE_NAME/VERSION).
PROTOCOL = 'flash'
PROTOCOL_VERSION = '1.0.0'

#: Endpoint the firmware service listens on (NBUS_FLASH_MAIN_EP).
MAIN_EP = 1

#: Largest read/write block the firmware accepts (NBUS_FLASH_BLOCK_LEN), the default of a FlashService.
BLOCK_LEN = 256

#: Block operation flags (flash_block_ops_t in services/interfaces/flash.h).
OPS_READ = 1 << 0
OPS_WRITE = 1 << 1
OPS_ERASE = 1 << 2


class FlashError(Exception):
	"""A flash volume operation failed."""


class FlashVolume(abc.ABC):
	"""One flash volume exported by a :class:`FlashService`. Addresses are relative to the volume."""

	#: The volume name clients select it by.
	name = None

	@abc.abstractmethod
	def sizes(self):
		"""A list of ``(size, ops)`` tuples: the volume size, the erase block sizes, the page size."""

	@abc.abstractmethod
	def read(self, addr, length):
		"""Read ``length`` bytes at ``addr``. Raises :class:`FlashError` on failure."""

	@abc.abstractmethod
	def erase(self, addr, length):
		"""Erase the blocks covering ``addr``..``addr + length``. Raises :class:`FlashError` on failure."""

	@abc.abstractmethod
	def write(self, addr, data):
		"""Program ``data`` at ``addr`` (already erased). Raises :class:`FlashError` on failure."""

	def close(self):
		"""Called when the session on the volume is closed, to release any per-session state."""


class FlashService:
	"""Serves the nbus-flash protocol for a list of :class:`FlashVolume` objects.

	Like the firmware, a single session is supported: opening a volume replaces the previous session.
	``reset`` is an optional callable resetting the target, called on the ``reset`` command. ``block_len``
	is the largest read/write block accepted, a service without the firmware's buffer limits may accept
	larger ones, e.g. to program whole flash algorithm pages over a debug probe.
	"""

	def __init__(self, volumes, reset=None, block_len=BLOCK_LEN):
		import cbor2

		self._cbor = cbor2
		self._volumes = {volume.name: volume for volume in volumes}
		self._reset = reset
		self.block_len = block_len
		self._volume = None
		self._sid = 0
		self._next_sid = 1

	def handle(self, data):
		"""Process one request datagram and return the reply datagram, or ``None`` if there is none.

		Like the firmware, a request which is not a map with a text command gets no reply, any other
		request is answered, with an empty map for an unknown command.
		"""
		try:
			req = self._cbor.loads(data)
		except Exception:
			return None
		if not isinstance(req, dict) or not isinstance(req.get('c'), str):
			return None

		handler = getattr(self, '_cc_' + req['c'], None)
		resp = handler(req) if handler is not None else {}
		return self._cbor.dumps(resp)

	def close(self):
		"""Close the session, if any."""
		if self._volume is not None:
			self._volume.close()
		self._volume = None
		self._sid = 0

	@staticmethod
	def _uint(req, key):
		value = req.get(key)
		if isinstance(value, bool) or not isinstance(value, int) or value < 0:
			return None
		return value

	def _session_ok(self, req):
		return self._volume is not None and self._uint(req, 'sid') == self._sid

	def _range_ok(self, addr, length):
		return addr is not None and length is not None and addr + length <= self._volume.sizes()[0][0]

	def _cc_list(self, req):
		return {'d': [{'n': name} for name in self._volumes]}

	def _cc_info(self, req):
		volume = self._volumes.get(req.get('n'))
		if volume is None:
			return {'err': 'device not found'}
		return {'sizes': [{'s': size, 'ops': ops} for size, ops in volume.sizes()]}

	def _cc_open(self, req):
		volume = self._volumes.get(req.get('n'))
		if volume is None:
			return {'err': 'device not found'}
		self.close()
		self._volume = volume
		self._sid = self._next_sid
		self._next_sid = (self._next_sid % 0xffffffff) + 1
		logger.info('opened flash volume %r, session %d', volume.name, self._sid)
		return {'sid': self._sid}

	def _cc_close(self, req):
		if self._volume is None:
			return {'err': 'no session opened'}
		sid = self._sid
		logger.info('closed flash volume %r, session %d', self._volume.name, sid)
		self.close()
		return {'sid': sid}

	def _cc_erase(self, req):
		if not self._session_ok(req):
			return {'err': 'session error'}
		addr, length = self._uint(req, 'addr'), self._uint(req, 'len')
		if not self._range_ok(addr, length):
			return {'err': 'bad arguments'}
		try:
			self._volume.erase(addr, length)
		except FlashError as e:
			logger.error('erasing %d bytes at 0x%08x failed: %s', length, addr, e)
			return {'err': 'erasing failed'}
		return {'ret': 'ok'}

	def _cc_write(self, req):
		if not self._session_ok(req):
			return {'err': 'session error'}
		addr, length, data = self._uint(req, 'addr'), self._uint(req, 'len'), req.get('d')
		if (not isinstance(data, bytes) or length is None or len(data) != length or length > self.block_len
		    or not self._range_ok(addr, length)):
			return {'err': 'bad arguments'}
		try:
			self._volume.write(addr, data)
		except FlashError as e:
			logger.error('writing %d bytes at 0x%08x failed: %s', length, addr, e)
			return {'err': 'writing failed'}
		return {'ret': 'ok'}

	def _cc_read(self, req):
		if not self._session_ok(req):
			return {'err': 'session error'}
		addr, length = self._uint(req, 'addr'), self._uint(req, 'len')
		if length is None or length > self.block_len or not self._range_ok(addr, length):
			return {'err': 'bad arguments'}
		try:
			data = self._volume.read(addr, length)
		except FlashError as e:
			logger.error('reading %d bytes at 0x%08x failed: %s', length, addr, e)
			return {'err': 'reading failed'}
		# Echo the requested address so the host can match the reply to its request.
		return {'addr': addr, 'd': bytes(data)}

	def _cc_usage(self, req):
		if not self._session_ok(req):
			return {'err': 'session error'}
		sizes = self._volume.sizes()
		erase_sizes = [size for size, ops in sizes if ops & OPS_ERASE]
		if not erase_sizes or sizes[0][0] < erase_sizes[-1]:
			return {'err': 'bad flash geometry'}
		erase_size = erase_sizes[-1]
		blocks = sizes[0][0] // erase_size
		ba = -(-blocks // (self.block_len * 8))
		bitmap = bytearray((-(-blocks // ba) + 7) // 8)
		for i in range(blocks):
			bit = i // ba
			if bitmap[bit // 8] & (1 << (bit % 8)):
				continue
			try:
				word = self._volume.read(i * erase_size, 4)
			except FlashError as e:
				logger.error('reading block at 0x%08x failed: %s', i * erase_size, e)
				return {'err': 'reading failed'}
			if bytes(word) != b'\xff\xff\xff\xff':
				bitmap[bit // 8] |= 1 << (bit % 8)
		return {'ba': ba, 'b': bytes(bitmap)}

	def _cc_reset(self, req):
		self.close()
		if self._reset is not None:
			self._reset()
		return {'ret': 'ok'}
