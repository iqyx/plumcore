# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 transport over a framed byte Stream (e.g. a serial line)
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""nbus2 over a byte stream: the host speaks the nbus2 wire protocol itself.

Unlike the UDP/IPv6 bridge, here there is a single shared medium (a serial line) carrying framed
nbus2 packets, and several nbus2 sockets are multiplexed over it. A background reader thread pulls
framed datagrams off the stream, decrypts/authenticates them into packets (see :mod:`pynbus2.packet`)
and dispatches each to the first socket whose binding matches - mirroring the firmware's ``nbus``
receive task. Transmission happens on the caller's thread under a lock, assigning the shared tx
counter.

The packet protection (BLAKE2s-SIV) is done here on the host, so this transport needs the preshared
key. The framing is supplied as a framer object, so the same transport works for any line/datagram
framing; today it is wired up for dgtext.
"""

import abc
import queue
import threading

from .base import Transport, SocketBackend
from .. import packet
from ..addressing import parse_id, parse_ep, format_id


class Stream(abc.ABC):
	"""A bidirectional byte stream the framer reads from and writes to."""

	@abc.abstractmethod
	def read_some(self, timeout):
		"""Return whatever bytes arrive within ``timeout`` seconds (possibly empty)."""

	@abc.abstractmethod
	def write(self, data):
		"""Write all of ``data`` to the stream."""

	def close(self):
		"""Release the underlying resource."""


class SerialStream(Stream):
	"""A :class:`Stream` backed by a serial port (via pyserial)."""

	def __init__(self, device, baudrate=115200, **kwargs):
		try:
			import serial
		except ImportError as e:
			raise ImportError(
				'the serial transport requires pyserial; install it with "pip install pyserial" '
				'or "pip install pynbus2[serial]"') from e
		# A short read timeout lets read_some poll without blocking the reader thread indefinitely.
		self._serial = serial.Serial(device, baudrate, timeout=0.05, **kwargs)

	def read_some(self, timeout):
		ser = self._serial
		ser.timeout = timeout
		first = ser.read(1)
		if not first:
			return b''
		# Drain whatever else already arrived so a whole frame is read in few calls.
		waiting = ser.in_waiting
		return first + ser.read(waiting) if waiting else first

	def write(self, data):
		self._serial.write(data)
		self._serial.flush()

	def close(self):
		self._serial.close()


class StreamTransport(Transport):
	"""Carries nbus2 packets over a framed :class:`Stream`, multiplexing several sockets.

	This base wires up the rx thread, dispatch, tx counter and crypto; concrete transports add a
	:meth:`from_uri` that constructs the stream and framer (see :class:`SerialDgtextTransport`).
	"""

	def __init__(self, stream, framer, ke, km, self_id, scheme=packet.Blake2sSiv,
	             schemes=packet.DEFAULT_SCHEMES, rx_tick=0.2):
		self._stream = stream
		self._framer = framer
		self._ke = ke
		self._km = km
		self._self_id = self_id
		self._scheme = scheme
		self._schemes = schemes
		self._rx_tick = rx_tick

		self._counter = 0
		self._tx_lock = threading.Lock()
		self._sockets = []
		self._sock_lock = threading.Lock()

		self._stop = threading.Event()
		self._rx_thread = threading.Thread(target=self._rx_loop, name='pynbus2-rx', daemon=True)
		self._rx_thread.start()

	# -- Transport API -------------------------------------------------------------------------

	def open_socket(self):
		sock = StreamSocket(self)
		with self._sock_lock:
			self._sockets.append(sock)
		return sock

	def close(self):
		self._stop.set()
		if self._rx_thread.is_alive():
			self._rx_thread.join(timeout=1.0)
		self._stream.close()

	# -- internals -----------------------------------------------------------------------------

	def _remove_socket(self, sock):
		with self._sock_lock:
			if sock in self._sockets:
				self._sockets.remove(sock)

	def _send(self, sock, data):
		src_id = sock.local_id if sock.local_id is not None else self._self_id
		src_ep = sock.local_ep
		with self._tx_lock:
			counter = self._counter & 0xffff
			self._counter += 1
			wire = packet.encode(data, sock.remote_id, sock.remote_ep, src_id, src_ep,
			                     counter, self._ke, self._km, self._scheme)
			self._framer.write_datagram(wire)

	def _rx_loop(self):
		while not self._stop.is_set():
			try:
				raw = self._framer.read_datagram(self._rx_tick)
			except Exception:
				if self._stop.is_set():
					break
				continue
			if raw is None:
				continue
			pkt = packet.decode(raw, self._ke, self._km, self._schemes)
			if pkt is None:
				continue
			self._dispatch(pkt)

	def _dispatch(self, pkt):
		with self._sock_lock:
			for sock in self._sockets:
				if sock.matches(pkt):
					sock.deliver(pkt)
					return


class StreamSocket(SocketBackend):
	"""A multiplexed nbus2 socket over a :class:`StreamTransport`."""

	def __init__(self, transport):
		self._transport = transport
		self.local_id = None
		self.local_ep = 0
		self.remote_id = None
		self.remote_ep = 0
		self._queue = queue.Queue()

	def bind(self, id, ep):
		self.local_id = parse_id(id)
		self.local_ep = parse_ep(ep)

	def connect(self, id, ep):
		self.remote_id = parse_id(id)
		self.remote_ep = parse_ep(ep)

	def send(self, data):
		if self.remote_id is None:
			raise OSError('socket is not connected to a destination')
		self._transport._send(self, data)

	def recv(self, timeout):
		try:
			pkt = self._queue.get(timeout=timeout) if timeout is not None else self._queue.get()
		except queue.Empty:
			return None
		return pkt.payload

	def close(self):
		self._transport._remove_socket(self)

	def matches(self, pkt):
		"""Whether a received packet belongs to this socket (by local binding and/or remote connection)."""
		if self.local_id is not None and (pkt.dst_id != self.local_id or pkt.dst_ep != self.local_ep):
			return False
		if self.remote_id is not None and (pkt.src_id != self.remote_id or pkt.src_ep != self.remote_ep):
			return False
		return True

	def deliver(self, pkt):
		"""Queue a packet dispatched to this socket by the transport's reader thread."""
		self._queue.put(pkt)

	def __repr__(self):
		remote = format_id(self.remote_id) if self.remote_id is not None else '-'
		local = format_id(self.local_id) if self.local_id is not None else '-'
		return '<StreamSocket %s:%d -> %s:%d>' % (local, self.local_ep, remote, self.remote_ep)
