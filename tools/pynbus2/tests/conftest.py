# SPDX-License-Identifier: GPL-3.0-or-later
#
# pynbus2 test fixtures
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import queue

import pytest

from pynbus2.transport.stream import Stream


class LoopStream(Stream):
	"""An in-memory :class:`Stream`: bytes fed with :meth:`feed` are read back, writes are collected."""

	def __init__(self):
		self._rx = queue.Queue()
		self.written = bytearray()

	def feed(self, data):
		self._rx.put(bytes(data))

	def read_some(self, timeout):
		try:
			return self._rx.get(timeout=timeout)
		except queue.Empty:
			return b''

	def write(self, data):
		self.written += data


@pytest.fixture
def loop_stream():
	return LoopStream()
