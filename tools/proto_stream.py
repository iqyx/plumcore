# SPDX-License-Identifier: GPL-3.0-or-later
#
# Host side client for the proto-stream service (reliable stream over datagrams)
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""File-like access to a device running the ``proto-stream`` service.

The service (``services/proto-stream``) carries a byte stream over unreliable datagrams using a simple
stop-and-wait, block numbered protocol. Every exchange is a CBOR map:

* ``{'get': n}`` requests the next outbound block from the device. A *new* block number consumes and
  returns fresh data as ``{'d': <bytes>}``; repeating the previous number retransmits the last block
  (so a lost reply costs nothing). An empty ``d`` means the device currently has nothing to send.
* ``{'put': n, 'd': <bytes>}`` uploads a block to the device, which always answers ``{'a': n}``. A new
  block number stores the data; repeating one is a harmless duplicate.
* Every reply also carries ``{'rdf': <device rx free bytes>, 'tda': <device tx bytes waiting>}``, which
  we use for flow control so an upload never overruns the device receive buffer.

Datagram delivery, addressing and retransmission are handled by :mod:`pynbus2` (the UDP/IPv6 bridge
transport); this module only implements the block protocol on top of a connected socket and presents
it through the familiar ``read``/``write`` API::

    import proto_stream

    with proto_stream.ProtoStream.connect('udp6:///00010002/4') as s:
        s.write(b'hello\\n')
        data = s.read(64)
"""

import logging
import time

import cbor2

logger = logging.getLogger(__name__)


class ProtoStream:
	"""A file-like/socket-like stream backed by the ``proto-stream`` service on a device.

	Wraps an already connected :class:`pynbus2.NbusSocket` (or anything exposing a compatible
	``request()``). Use :meth:`connect` to build one straight from a pynbus2 URI.
	"""

	#: Maximum payload carried in a single block, matching ``PROTO_STREAM_BLOCK_LEN`` on the device.
	BLOCK_LEN = 220

	def __init__(self, sock, timeout=None, retries=50, poll_interval=0.02):
		self._sock = sock
		# Set by connect() when this object owns the pynbus2 connection and must close it too.
		self._owner = None
		self._timeout = timeout if timeout is not None else getattr(sock, 'request_timeout', 0.1)
		self._retries = retries
		self._poll_interval = poll_interval

		# Block sequence numbers. The device starts both its last-seen counters at 0, so the first
		# request in each direction must be non-zero to avoid looking like a retransmit/duplicate.
		self._get_seq = 0
		self._put_seq = 0

		# Latest flow-control figures reported by the device (None until the first exchange).
		self._rdf = None
		self._tda = None

		# Data fetched from the device but not yet handed to the caller (a block can be larger than
		# the requested read size).
		self._rxbuf = bytearray()
		self._closed = False

	@classmethod
	def connect(cls, uri, id=None, ep=None, **kwargs):
		"""Open a pynbus2 connection for ``uri`` and wrap a socket to ``(id, ep)`` as a stream.

		``id``/``ep`` may be omitted when the URI path already carries a default destination
		(e.g. ``udp6:///00010002/4``). The returned stream owns the connection and closes it on
		:meth:`close`.
		"""
		import pynbus2
		nbus = pynbus2.connect(uri)
		stream = cls(nbus.socket(id, ep), **kwargs)
		stream._owner = nbus
		return stream

	# -- protocol ----------------------------------------------------------------------------------

	def _transact(self, msg):
		"""Send one CBOR request and return the decoded reply map, updating the flow-control cache."""
		reply = self._sock.request(cbor2.dumps(msg), timeout=self._timeout, retries=self._retries)
		if reply is None:
			raise TimeoutError('proto-stream: no response from device')
		resp = cbor2.loads(reply)
		if not isinstance(resp, dict):
			raise OSError('proto-stream: malformed response %r' % (resp,))
		if 'rdf' in resp:
			self._rdf = resp['rdf']
		if 'tda' in resp:
			self._tda = resp['tda']
		return resp

	def _fetch(self):
		"""Request the next outbound block. Returns the bytes received, or ``b''`` if none are ready."""
		self._get_seq += 1
		resp = self._transact({'get': self._get_seq})
		d = resp.get('d')
		return bytes(d) if d else b''

	def _wait_rx_space(self, need):
		"""Block until the device reports at least ``need`` free bytes in its receive buffer."""
		while self._rdf is None or self._rdf < need:
			self._transact({})
			if self._rdf is not None and self._rdf >= need:
				return
			time.sleep(self._poll_interval)

	# -- flow control introspection ----------------------------------------------------------------

	def poll(self):
		"""Refresh the cached flow-control figures with an empty request. Returns ``(rdf, tda)``."""
		self._transact({})
		return self._rdf, self._tda

	@property
	def readable_bytes(self):
		"""Device tx bytes waiting to be read, as of the last exchange (``None`` if never polled)."""
		return self._tda

	@property
	def writable_bytes(self):
		"""Free space in the device rx buffer, as of the last exchange (``None`` if never polled)."""
		return self._rdf

	# -- file-like API -----------------------------------------------------------------------------

	def write(self, data):
		"""Upload ``data`` to the device, blocking until every byte is acknowledged. Returns the count."""
		self._check_open()
		mv = memoryview(bytes(data))
		total = 0
		while len(mv):
			chunk = bytes(mv[:self.BLOCK_LEN])
			self._wait_rx_space(len(chunk))
			self._put_seq += 1
			resp = self._transact({'put': self._put_seq, 'd': chunk})
			if resp.get('a') != self._put_seq:
				raise OSError('proto-stream: unexpected ack %r for block %d' % (resp.get('a'), self._put_seq))
			total += len(chunk)
			mv = mv[len(chunk):]
		return total

	def read(self, size=-1):
		"""Read up to ``size`` bytes (all currently available when ``size`` < 0).

		Blocks, polling the device, until at least one byte can be returned; like a socket ``recv`` it
		may return fewer than ``size`` bytes once the device momentarily runs dry.
		"""
		self._check_open()
		if size == 0:
			return b''
		out = bytearray()
		while True:
			if self._rxbuf:
				if size < 0:
					out += self._rxbuf
					self._rxbuf = bytearray()
				else:
					take = min(size - len(out), len(self._rxbuf))
					out += self._rxbuf[:take]
					del self._rxbuf[:take]
					if len(out) >= size:
						return bytes(out)
			chunk = self._fetch()
			if chunk:
				self._rxbuf += chunk
				continue
			# The device has nothing more for now.
			if size < 0 or out:
				return bytes(out)
			time.sleep(self._poll_interval)

	def readline(self, size=-1):
		"""Read and return one line including the trailing newline, or up to ``size`` bytes."""
		self._check_open()
		line = bytearray()
		while size < 0 or len(line) < size:
			chunk = self.read(1)
			if not chunk:
				break
			line += chunk
			if chunk == b'\n':
				break
		return bytes(line)

	def readall(self):
		"""Read everything the device currently has buffered and return it."""
		return self.read(-1)

	def flush(self):
		"""No-op: :meth:`write` already blocks until each block is acknowledged."""
		self._check_open()

	def readable(self):
		return True

	def writable(self):
		return True

	def seekable(self):
		return False

	def close(self):
		"""Close the stream, and the underlying pynbus2 connection if this stream owns it."""
		if self._closed:
			return
		self._closed = True
		if self._owner is not None:
			self._owner.close()
		else:
			self._sock.close()

	@property
	def closed(self):
		return self._closed

	def _check_open(self):
		if self._closed:
			raise ValueError('I/O operation on closed stream')

	def __enter__(self):
		return self

	def __exit__(self, *exc):
		self.close()
