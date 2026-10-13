# SPDX-License-Identifier: GPL-3.0-or-later
#
# ssh transport tests against a local pynbus2-ssh-proxy serving a fake transport
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import os
import queue
import stat
import sys

import pytest

pytest.importorskip('cbor2')

import pynbus2
from pynbus2.transport import register
from pynbus2.transport.base import Transport, SocketBackend


class FakeTransport(Transport):
	"""``fake://<device>[/<interface>]``: interfaces echoing the datagrams prefixed with their name."""

	INTERFACES = {'': {'p': 'root'}, 'conf': {'p': 'conf', 'pv': '1.0.0'}}

	@classmethod
	def from_uri(cls, uri):
		transport = cls()
		name = ' '.join(uri.path_segments)
		transport.default_dst = (name, 0) if name or uri.get('root') != '0' else None
		return transport

	@classmethod
	def split_uri(cls, uri):
		dst = (' '.join(uri.path_segments), 0) if uri.path_segments else None
		key = (uri.scheme, uri.host, tuple(sorted((k, tuple(v)) for k, v in uri.query.items())))
		return key, dst

	@classmethod
	def probe(cls, uri):
		return ['fake://dev1?name=Device', 'fake://dev2']

	def walk(self):
		return list(self.INTERFACES.items())

	def open_socket(self):
		return FakeSocket()


class FakeSocket(SocketBackend):

	def __init__(self):
		self._name = None
		self._queue = queue.Queue()

	def bind(self, id, ep):
		pass

	def connect(self, id, ep):
		if id not in FakeTransport.INTERFACES:
			raise pynbus2.NbusError('no interface %r' % id)
		self._name = id

	def send(self, data):
		self._queue.put(self._name.encode() + b':' + data)

	def recv(self, timeout):
		try:
			return self._queue.get(timeout=timeout)
		except queue.Empty:
			return None

	def close(self):
		pass


def serve():
	"""The fake ``ssh`` command: the proxy of the fake transport on stdio."""
	from pynbus2.ssh_proxy import main
	register('fake', FakeTransport)
	sys.exit(main(['-t', 'fake']))


@pytest.fixture
def fake_ssh(tmp_path, monkeypatch):
	"""An ``ssh`` command on the PATH running the proxy locally, ignoring its arguments."""
	ssh = tmp_path / 'ssh'
	tests = os.path.dirname(os.path.abspath(__file__))
	ssh.write_text('#!/bin/sh\necho motd\nexec %s -c "import sys; sys.path.insert(0, %r); '
	               'import test_ssh; test_ssh.serve()"\n' % (sys.executable, tests))
	ssh.chmod(ssh.stat().st_mode | stat.S_IEXEC)
	monkeypatch.setenv('PATH', '%s%s%s' % (tmp_path, os.pathsep, os.environ['PATH']))


def test_probe(fake_ssh):
	assert pynbus2.probe('ssh://nbus@daq:23') == ['ssh://nbus@daq:23/dev1?name=Device', 'ssh://nbus@daq:23/dev2']


def test_default_destination(fake_ssh):
	with pynbus2.connect('ssh://daq/dev1?name=Device') as nbus:
		sock = nbus.socket()
		sock.send(b'x')
		assert sock.recv(2.0) == b':x'


def test_uri_destination(fake_ssh):
	with pynbus2.connect('ssh://daq/dev1/conf') as nbus:
		sock = nbus.socket()
		sock.send(b'x')
		assert sock.recv(2.0) == b'conf:x'


def test_no_default_destination(fake_ssh):
	with pynbus2.connect('ssh://daq/dev1?root=0') as nbus:
		with pytest.raises(pynbus2.NotConnectedError):
			nbus.socket().send(b'x')


def test_walk_and_named_interfaces(fake_ssh):
	with pynbus2.connect('ssh://daq/dev1') as nbus:
		assert nbus.walk() == list(FakeTransport.INTERFACES.items())
		root = nbus.socket()
		conf = nbus.socket('conf', 0)
		conf.send(b'a')
		root.send(b'b')
		assert conf.recv(2.0) == b'conf:a'
		assert root.recv(2.0) == b':b'
		with pytest.raises(pynbus2.NbusError, match='no interface'):
			nbus.socket('missing', 0)
