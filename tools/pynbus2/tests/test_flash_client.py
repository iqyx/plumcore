# SPDX-License-Identifier: GPL-3.0-or-later
#
# Flash client tests
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import io
import queue
import threading

import pytest

cbor2 = pytest.importorskip('cbor2')

from pynbus2 import flash as flash_client
from pynbus2.flash import Aborted, FlashClient, FlashError
from pynbus2.services import flash

from test_flash_service import PAGE, SECTOR, SIZE, RamVolume


class ServiceSocket:
	"""The part of a NbusSocket the client uses, answered by a FlashService. ``hook(req, reply)`` may change
	the reply datagrams queued for a request, by default the reply itself."""

	request_timeout = 0.05

	def __init__(self, service, hook=None):
		self.service = service
		self.hook = hook
		self.replies = queue.Queue()
		self.requests = []

	def send(self, data):
		req = cbor2.loads(data)
		self.requests.append(req)
		reply = self.service.handle(data)
		for datagram in (self.hook(req, reply) if self.hook else [reply]):
			self.replies.put(datagram)

	def recv(self, timeout):
		try:
			return self.replies.get(timeout=timeout)
		except queue.Empty:
			return None

	def flush(self):
		dropped = 0
		while not self.replies.empty():
			self.replies.get()
			dropped += 1
		return dropped


@pytest.fixture
def volume():
	return RamVolume()


@pytest.fixture
def client(volume):
	return FlashClient(ServiceSocket(flash.FlashService([volume])))


def test_list_and_info(client):
	assert client.list() == ['ram']
	assert client.info('ram') == (SIZE, SECTOR, PAGE)
	with pytest.raises(FlashError, match='device not found'):
		client.info('nope')


def test_requests_carry_ids(client):
	client.list()
	client.list()
	assert [req['id'] for req in client._socket.requests] == [1, 2]


def test_write_read_erase(client, volume):
	data = bytes(range(256)) * 3 + b'abc'
	stages = []
	client.write('ram', SIZE, SECTOR, PAGE, data, stage=lambda what, total: stages.append((what, total)))
	padded = data + b'\xff' * (-len(data) % PAGE)
	assert [s for s, _ in stages] == ['erase', 'write', 'verify']
	assert bytes(volume.mem[:len(padded)]) == padded

	out = io.BytesIO()
	progress = []
	client.read('ram', SIZE, out, progress=progress.append)
	assert out.getvalue() == bytes(volume.mem)
	assert progress[-1] == SIZE

	client.erase('ram', SIZE, SECTOR)
	assert volume.mem == b'\xff' * SIZE


def test_read_stops_at_an_erased_sector(client, volume):
	volume.mem[:10] = b'x' * 10
	out = io.BytesIO()
	client.read('ram', SIZE, out, stop_sector=SECTOR)
	# The erased sector it stops at is read too.
	assert len(out.getvalue()) == 2 * SECTOR


def test_usage(client, volume):
	volume.mem[2 * SECTOR] = 0
	volume.mem[3 * SECTOR] = 0
	assert client.usage('ram', SIZE, SECTOR) == (0.5, [(0.5, 1.0)])


def test_write_too_large(client):
	with pytest.raises(FlashError, match='do not fit'):
		client.write('ram', SIZE, SECTOR, PAGE, b'\x00' * (SIZE + 1))


def test_late_reply_dropped(volume):
	"""A late reply to the previous request arrives before the reply to the current one."""
	previous = []

	def hook(req, reply):
		datagrams = previous[-1:] + [reply]
		previous.append(reply)
		return datagrams

	socket = ServiceSocket(flash.FlashService([volume]), hook)
	client = FlashClient(socket)
	assert client.list() == ['ram']
	assert client.info('ram') == (SIZE, SECTOR, PAGE)


def test_retransmit_after_a_lost_reply(volume):
	lost = []

	def hook(req, reply):
		if not lost:
			lost.append(req)
			return []
		return [reply]

	client = FlashClient(ServiceSocket(flash.FlashService([volume]), hook))
	assert client.list() == ['ram']
	assert len(client._socket.requests) == 2


def test_replies_without_id_accepted(volume):
	def hook(req, reply):
		r = cbor2.loads(reply)
		r.pop('id', None)
		return [cbor2.dumps(r)]

	client = FlashClient(ServiceSocket(flash.FlashService([volume]), hook))
	assert client.list() == ['ram']


def test_no_response(volume):
	client = FlashClient(ServiceSocket(flash.FlashService([volume]), lambda req, reply: []))
	with pytest.raises(FlashError, match='no response'):
		client.call({'c': 'list'}, retries=2)
	assert len(client._socket.requests) == 2


def test_nested_session_refused(client):
	with client.session('ram'):
		with pytest.raises(FlashError, match='opened already'):
			with client.session('ram'):
				pass


def test_sessions_serialized(client, volume):
	"""A thread opening a partition waits for the session of another one, which the single session
	service would close otherwise."""
	opened = threading.Event()
	release = threading.Event()
	order = []

	def first():
		with client.session('ram'):
			order.append('first')
			opened.set()
			release.wait(5)
			order.append('first done')

	thread = threading.Thread(target=first)
	thread.start()
	opened.wait(5)
	second = threading.Thread(target=lambda: client.read('ram', PAGE, io.BytesIO()) or order.append('second'))
	second.start()
	second.join(0.3)
	assert second.is_alive()
	release.set()
	thread.join(5)
	second.join(5)
	assert order == ['first', 'first done', 'second']


def test_abort_while_waiting(client, monkeypatch):
	monkeypatch.setattr(FlashClient, 'ABORT_POLL', 0.01)
	opened = threading.Event()
	release = threading.Event()

	def hold():
		with client.session('ram'):
			opened.set()
			release.wait(5)

	thread = threading.Thread(target=hold)
	thread.start()
	opened.wait(5)
	abort = threading.Event()
	abort.set()
	with pytest.raises(Aborted):
		client.erase('ram', SIZE, SECTOR, abort=abort)
	release.set()
	thread.join(5)


def test_abort_closes_the_session(client):
	abort = threading.Event()

	def progress(done):
		abort.set()

	with pytest.raises(Aborted):
		client.read('ram', SIZE, io.BytesIO(), progress=progress, abort=abort)
	assert client._socket.requests[-1]['c'] == 'close'
	# The session lock is released.
	assert client.list() == ['ram']
	client.read('ram', PAGE, io.BytesIO())
