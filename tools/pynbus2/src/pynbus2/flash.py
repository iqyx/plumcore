# SPDX-License-Identifier: GPL-3.0-or-later
#
# Client of the nbus-flash service protocol
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""A client of the flash partitions (volumes) exported by an interface speaking the ``flash`` protocol: the
firmware ``services/nbus-flash`` service, or the virtual one of the ``swd`` transport
(:class:`pynbus2.services.flash.FlashService`, which documents the protocol).

    with pynbus2.connect('rible://nwdaq-hh1/flash') as nbus:
        client = FlashClient(nbus.socket())
        size, erase_size, page_size = client.info('calib')
        client.read('calib', size, file)

The client may be shared by several threads. Each request/response exchange is serialized and so is each
session on an opened partition: a session is held for a whole operation (e.g. erasing, writing and verifying
a partition) and the other threads wait for it. The firmware supports several sessions, but the older
firmware and the virtual service support a single one, opening a partition there closes the session opened
before. Partitions cannot be opened in a nested way for the same reason.

Every request carries an ``"id"``, which the firmware echoes, so a late reply to an earlier (retransmitted)
request is not taken for the reply of the current one. Replies without it are accepted.
"""

import contextlib
import logging
import threading
import time

import cbor2

from .errors import NbusError
from .services.flash import BLOCK_LEN, OPS_ERASE, OPS_WRITE

logger = logging.getLogger(__name__)


class FlashError(NbusError):
	"""A flash request failed: there is no valid response, the device reported an error, or the data does
	not fit."""


class Aborted(NbusError):
	"""A flash operation was aborted."""


class FlashClient:
	"""A client of a flash interface the ``socket`` (a :class:`pynbus2.NbusSocket`) is connected to. The
	client must be the only user of the socket. ``mtu`` is the MTU the interface advertises, it allows
	writing pages larger than :attr:`TRANSFER_BLOCK` at once, e.g. the whole flash algorithm pages over a
	debug probe.

	The ``abort`` argument of the operations is a :class:`threading.Event`, setting it stops the operation
	before its next block, or while it waits for another thread's session, with :class:`Aborted`.
	``progress`` is called with the number of bytes done so far after each block.
	"""

	#: Number of attempts of a request before giving up.
	RETRIES = 10

	#: How long the usage estimate may take, in seconds. It reads every erase block of the partition.
	USAGE_TIMEOUT = 10.0

	#: How long erasing a single erase block may take, in seconds.
	ERASE_TIMEOUT = 5.0

	#: Number of attempts to erase a block. Erasing a block again is harmless.
	ERASE_RETRIES = 3

	#: Size of the blocks read and written at once. The device accepts up to NBUS_FLASH_BLOCK_LEN (256 B),
	#: but the dgtext serial link caps a whole datagram at 256 B including the nbus2 header and the CBOR
	#: framing, so 128 B works on every transport.
	TRANSFER_BLOCK = 128

	#: Room for the request framing in a message, the largest block is the interface MTU less this (as in
	#: the firmware, NBUS_FLASH_RX_BUF_LEN - NBUS_FLASH_BLOCK_LEN).
	FRAMING = 64

	#: How often a thread waiting for the session of another one checks its ``abort``, in seconds.
	ABORT_POLL = 0.1

	def __init__(self, socket, mtu=None):
		self._socket = socket
		self._max_block = mtu - self.FRAMING if mtu else BLOCK_LEN
		# Serializes the request/response exchanges.
		self._lock = threading.Lock()
		self._id = 0
		# Serializes the sessions, held by the thread in _session_owner.
		self._session_lock = threading.Lock()
		self._session_owner = None

	def call(self, req, timeout=None, retries=None):
		"""Send the request map ``req`` and return the response map. Raises :class:`FlashError` if there is
		no valid response or it reports an error.

		Without ``timeout`` the retransmit interval suits the transport (the socket's ``request_timeout``),
		e.g. a BLE link is much slower than UDP. A request is sent ``retries`` times at most, :attr:`RETRIES`
		by default.
		"""
		timeout = timeout or self._socket.request_timeout
		with self._lock:
			self._id = self._id % 0xffffffff + 1
			id = self._id
			payload = cbor2.dumps(dict(req, id=id))
			# Replies to the earlier requests left over, e.g. duplicates produced by retransmits.
			self._socket.flush()
			for _ in range(retries or self.RETRIES):
				self._socket.send(payload)
				r = self._receive(id, timeout)
				if r is not None:
					break
			else:
				raise FlashError('no response')
		if r.get('err'):
			raise FlashError(r['err'])
		return r

	def _receive(self, id, timeout):
		"""The response map to the request ``id`` arriving within ``timeout`` seconds, ``None`` if there is
		none. Undecodable responses and responses to other requests are dropped."""
		deadline = time.monotonic() + timeout
		while (remaining := deadline - time.monotonic()) > 0:
			reply = self._socket.recv(remaining)
			if reply is None:
				return None
			try:
				r = cbor2.loads(reply)
			except Exception:
				logger.debug('dropping an undecodable response')
				continue
			if not isinstance(r, dict):
				logger.debug('dropping a response which is not a map')
				continue
			if r.get('id', id) != id:
				logger.debug('dropping a late response to request %s', r.get('id'))
				continue
			return r
		return None

	def list(self):
		"""The names of the flash partitions on the device."""
		return [d.get('n', '') for d in self.call({'c': 'list'}).get('d', [])]

	def info(self, name):
		"""The geometry of the partition ``name`` as ``(size, erase block size, page size)``. The erase
		block size is the smallest one, which the usage is estimated in, the page size the smallest
		programmable unit; each ``None`` if the partition cannot be erased or written."""
		sizes = [(s.get('s', 0), s.get('ops', 0)) for s in self.call({'c': 'info', 'n': name}).get('sizes', [])]
		if not sizes:
			raise FlashError('no sizes reported')
		erase_sizes = [size for size, ops in sizes[1:] if ops & OPS_ERASE]
		page_sizes = [size for size, ops in sizes[1:] if ops & OPS_WRITE]
		return sizes[0][0], (erase_sizes[-1] if erase_sizes else None), (page_sizes[-1] if page_sizes else None)

	def usage(self, name, size, erase_size, abort=None):
		"""The erase blocks of the partition ``name`` in use as ``(share, ranges)``: the share from 0 to 1
		and the used ranges as ``(start, end)`` tuples relative to the partition size, in address order.
		``size`` and ``erase_size`` are the partition geometry from :meth:`info`."""
		with self.session(name, abort) as sid:
			# The device replies once the estimate is done, retransmits would only queue more of them.
			r = self.call({'c': 'usage', 'sid': sid}, timeout=self.USAGE_TIMEOUT, retries=1)
		ba = r.get('ba', 1)
		bitmap = r.get('b', b'')
		blocks = size // erase_size
		if not blocks:
			return 0.0, []
		# Each bit covers ba blocks, the last one possibly fewer. Join the adjacent used ones to ranges.
		used = 0
		ranges = []
		for bit in range(-(-blocks // ba)):
			if bit // 8 >= len(bitmap) or not bitmap[bit // 8] & (1 << (bit % 8)):
				continue
			start, end = bit * ba, min(blocks, (bit + 1) * ba)
			used += end - start
			if ranges and ranges[-1][1] == start:
				ranges[-1][1] = end
			else:
				ranges.append([start, end])
		return used / blocks, [(start / blocks, end / blocks) for start, end in ranges]

	def erase(self, name, size, erase_size, progress=None, abort=None):
		"""Erase the whole partition ``name`` one erase block after another. ``size`` and ``erase_size``
		are the partition geometry from :meth:`info`."""
		with self.session(name, abort) as sid:
			self._erase(sid, size, erase_size, progress, abort)

	def _erase(self, sid, size, erase_size, progress, abort):
		for addr in range(0, size, erase_size):
			_check(abort)
			length = min(erase_size, size - addr)
			try:
				self.call({'c': 'erase', 'sid': sid, 'addr': addr, 'len': length},
				          timeout=self.ERASE_TIMEOUT, retries=self.ERASE_RETRIES)
			except FlashError as e:
				raise FlashError(f'erasing at 0x{addr:08x} failed: {e}')
			if progress is not None:
				progress(addr + length)

	def write(self, name, size, erase_size, page_size, data, progress=None, abort=None, stage=None):
		"""Write ``data`` to the start of the partition ``name``, erasing the whole partition first and
		reading the data back to verify it at the end. ``size``, ``erase_size`` and ``page_size`` are the
		partition geometry from :meth:`info`. The data is padded with 0xff to whole pages.

		``stage`` is called as ``stage(what, total)`` when erasing (``'erase'``, the partition size),
		writing and verifying (``'write'`` and ``'verify'``, the padded data size) starts, ``progress`` with
		the bytes done so far in the stage.
		"""
		# Whole pages in each transfer block, a single page if it is larger.
		block = page_size * max(1, self.TRANSFER_BLOCK // page_size)
		if block > self._max_block:
			raise FlashError(f'page size {page_size} B is larger than the {self._max_block} B blocks the interface takes')
		data = bytes(data) + b'\xff' * (-len(data) % page_size)
		if len(data) > size:
			raise FlashError(f'{len(data)} B do not fit the partition of {size} B')

		with self.session(name, abort) as sid:
			if stage is not None:
				stage('erase', size)
			self._erase(sid, size, erase_size, progress, abort)
			if stage is not None:
				stage('write', len(data))
			for addr in range(0, len(data), block):
				_check(abort)
				chunk = data[addr:addr + block]
				try:
					self.call({'c': 'write', 'sid': sid, 'addr': addr, 'len': len(chunk), 'd': chunk})
				except FlashError as e:
					# If the reply to a write was lost, its retransmit fails as the block is already
					# programmed. The block is fine if it reads back as written.
					if self._read_block(sid, addr, len(chunk), check=False) != chunk:
						raise FlashError(f'writing at 0x{addr:08x} failed: {e}')
				if progress is not None:
					progress(addr + len(chunk))

			if stage is not None:
				stage('verify', len(data))
			for addr in range(0, len(data), self.TRANSFER_BLOCK):
				_check(abort)
				expected = data[addr:addr + self.TRANSFER_BLOCK]
				got = self._read_block(sid, addr, len(expected))
				if got != expected:
					first = next(i for i in range(len(expected)) if got[i] != expected[i])
					hint = ', the block reads as erased' if got == b'\xff' * len(got) else ''
					raise FlashError(f'verification failed at 0x{addr + first:08x}: read 0x{got[first]:02x}, '
					                 f'expected 0x{expected[first]:02x}{hint}')
				if progress is not None:
					progress(addr + len(expected))

	def read(self, name, size, file, progress=None, abort=None, stop_sector=None):
		"""Read the whole partition ``name`` of ``size`` bytes into the binary ``file`` object, one block of
		:attr:`TRANSFER_BLOCK` bytes after another.

		With ``stop_sector``, a sector size in bytes (e.g. the erase size), reading ends after the first whole
		sector aligned to it which is erased, all its bytes 0xff: the rest of the partition is taken as
		erased, e.g. after the data written to its start. The sector is written to ``file`` too."""
		# The bytes read of the current sector.
		sector = bytearray()
		with self.session(name, abort) as sid:
			for addr in range(0, size, self.TRANSFER_BLOCK):
				_check(abort)
				length = min(self.TRANSFER_BLOCK, size - addr)
				data = self._read_block(sid, addr, length)
				file.write(data)
				if progress is not None:
					progress(addr + length)
				if stop_sector:
					sector += data
					while len(sector) >= stop_sector:
						if sector.count(0xff, 0, stop_sector) == stop_sector:
							return
						del sector[:stop_sector]

	def _read_block(self, sid, addr, length, check=True):
		"""Read ``length`` bytes at ``addr`` in the session ``sid``. Raises :class:`FlashError` if it fails,
		or returns ``None`` instead if not ``check``."""
		try:
			r = self.call({'c': 'read', 'sid': sid, 'addr': addr, 'len': length})
			data = r.get('d')
			# A late reply to a retransmitted request may answer an earlier one, if the device does not echo
			# the request id.
			if r.get('addr') != addr or not isinstance(data, bytes) or len(data) != length:
				raise FlashError('unexpected response')
		except FlashError as e:
			if not check:
				return None
			raise FlashError(f'reading at 0x{addr:08x} failed: {e}')
		return data

	@contextlib.contextmanager
	def session(self, name, abort=None):
		"""Open the partition ``name`` for the ``with`` block, giving the session id. It waits until the
		session of another thread ends. Raises :class:`FlashError` if the partition cannot be opened or the
		thread has one opened already."""
		if self._session_owner == threading.get_ident():
			raise FlashError('a partition is opened already')
		while not self._session_lock.acquire(timeout=self.ABORT_POLL):
			_check(abort)
		self._session_owner = threading.get_ident()
		try:
			_check(abort)
			sid = self.call({'c': 'open', 'n': name})['sid']
			try:
				yield sid
			finally:
				try:
					self.call({'c': 'close', 'sid': sid})
				except Exception as e:
					# E.g. the link lost, the error of the block is the one to report.
					logger.debug('closing session %s failed: %s', sid, e)
		finally:
			self._session_owner = None
			self._session_lock.release()


def _check(abort):
	if abort is not None and abort.is_set():
		raise Aborted()
