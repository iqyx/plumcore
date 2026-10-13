# SPDX-License-Identifier: GPL-3.0-or-later
#
# ssh transport tests against a local SSH server running the pynbus2-ssh-proxy of a fake transport
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import queue
import socket
import threading

import pytest

pytest.importorskip('cbor2')
paramiko = pytest.importorskip('paramiko')

import pynbus2
from pynbus2.transport import register, ssh
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



class FakeSshServer(paramiko.ServerInterface):
	"""A local SSH server with a password login (or a public key one), the shell session runs the proxy of the
	fake transport."""

	USER = 'nbus'
	PASSWORD = 'secret'

	def __init__(self):
		self.key = paramiko.ECDSAKey.generate()
		#: The client key accepted, the password is not when set.
		self.client_key = None
		self._sock = socket.socket()
		self._sock.bind(('127.0.0.1', 0))
		self._sock.listen()
		self.port = self._sock.getsockname()[1]
		threading.Thread(target=self._accept_loop, daemon=True).start()

	def uri(self, path=''):
		return 'ssh://%s@127.0.0.1:%d%s' % (self.USER, self.port, path)

	def close(self):
		self._sock.close()

	def _accept_loop(self):
		while True:
			try:
				conn, _ = self._sock.accept()
			except OSError:
				return
			transport = paramiko.Transport(conn)
			transport.add_server_key(self.key)
			try:
				transport.start_server(server=self)
			except (paramiko.SSHException, OSError):
				# The client hung up, e.g. refusing the host key.
				transport.close()

	def get_allowed_auths(self, username):
		return 'publickey' if self.client_key else 'password'

	def check_auth_publickey(self, username, key):
		ok = username == self.USER and self.client_key is not None and key == self.client_key
		return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED

	def check_auth_password(self, username, password):
		ok = username == self.USER and password == self.PASSWORD and self.client_key is None
		return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED

	def check_channel_request(self, kind, chanid):
		return paramiko.OPEN_SUCCEEDED

	def check_channel_shell_request(self, channel):
		threading.Thread(target=self._shell, args=(channel,), daemon=True).start()
		return True

	def _shell(self, channel):
		from pynbus2.ssh_proxy import serve
		serve(channel.makefile('rb'), channel.makefile('wb'), 'fake')
		channel.send_exit_status(0)
		channel.close()


@pytest.fixture
def ssh_home(tmp_path, monkeypatch):
	"""An empty home directory without SSH keys, known hosts or an agent, and no prompts."""
	monkeypatch.setenv('HOME', str(tmp_path))
	monkeypatch.delenv('SSH_AUTH_SOCK', raising=False)
	monkeypatch.delenv(ssh.PASSWORD_ENV, raising=False)
	monkeypatch.delenv(ssh.TRUST_NEW_ENV, raising=False)
	register('fake', FakeTransport)
	yield tmp_path
	ssh.set_prompts()


@pytest.fixture
def server(ssh_home):
	server = FakeSshServer()
	yield server
	server.close()


@pytest.fixture
def trusted(server, monkeypatch):
	"""The server with its password in the environment and its host key trusted."""
	monkeypatch.setenv(ssh.PASSWORD_ENV, server.PASSWORD)
	monkeypatch.setenv(ssh.TRUST_NEW_ENV, '1')
	return server


def test_probe(trusted):
	assert pynbus2.probe(trusted.uri()) == [trusted.uri('/dev1?name=Device'), trusted.uri('/dev2')]


def test_default_destination(trusted):
	with pynbus2.connect(trusted.uri('/dev1?name=Device')) as nbus:
		sock = nbus.socket()
		sock.send(b'x')
		assert sock.recv(2.0) == b':x'


def test_uri_destination(trusted):
	with pynbus2.connect(trusted.uri('/dev1/conf')) as nbus:
		sock = nbus.socket()
		sock.send(b'x')
		assert sock.recv(2.0) == b'conf:x'


def test_no_default_destination(trusted):
	with pynbus2.connect(trusted.uri('/dev1?root=0')) as nbus:
		with pytest.raises(pynbus2.NotConnectedError):
			nbus.socket().send(b'x')


def test_walk_and_named_interfaces(trusted):
	with pynbus2.connect(trusted.uri('/dev1')) as nbus:
		assert nbus.walk() == list(FakeTransport.INTERFACES.items())
		root = nbus.socket()
		conf = nbus.socket('conf', 0)
		conf.send(b'a')
		root.send(b'b')
		assert conf.recv(2.0) == b'conf:a'
		assert root.recv(2.0) == b':b'
		with pytest.raises(pynbus2.NbusError, match='no interface'):
			nbus.socket('missing', 0)


def test_unknown_host_key_refused(server, ssh_home, monkeypatch):
	monkeypatch.setenv(ssh.PASSWORD_ENV, server.PASSWORD)
	with pytest.raises(pynbus2.NbusError, match='is not known'):
		pynbus2.connect(server.uri('/dev1'))
	assert not (ssh_home / '.ssh' / 'known_hosts').exists()


def test_trusted_host_key_remembered(trusted, ssh_home, monkeypatch):
	pynbus2.connect(trusted.uri('/dev1')).close()
	known = (ssh_home / '.ssh' / 'known_hosts').read_text()
	assert known.startswith('[127.0.0.1]:%d ecdsa-sha2-nistp256 ' % trusted.port)
	# Known now, no longer trusted by the environment.
	monkeypatch.delenv(ssh.TRUST_NEW_ENV)
	pynbus2.connect(trusted.uri('/dev1')).close()


def test_host_key_prompt(server, monkeypatch):
	monkeypatch.setenv(ssh.PASSWORD_ENV, server.PASSWORD)
	asked = []
	ssh.set_prompts(host_key=lambda host, key_type, fp: asked.append((host, key_type, fp)) or True)
	pynbus2.connect(server.uri('/dev1')).close()
	assert asked == [('[127.0.0.1]:%d' % server.port, 'ecdsa-sha2-nistp256', ssh.fingerprint(server.key))]


def test_host_key_prompt_refused(server, monkeypatch):
	monkeypatch.setenv(ssh.PASSWORD_ENV, server.PASSWORD)
	ssh.set_prompts(host_key=lambda host, key_type, fp: False)
	with pytest.raises(pynbus2.NbusError, match='is not known'):
		pynbus2.connect(server.uri('/dev1'))


def test_password_missing(server, monkeypatch):
	monkeypatch.setenv(ssh.TRUST_NEW_ENV, '1')
	with pytest.raises(pynbus2.NbusError, match=ssh.PASSWORD_ENV):
		pynbus2.connect(server.uri('/dev1'))


def test_password_prompt(server, monkeypatch):
	monkeypatch.setenv(ssh.TRUST_NEW_ENV, '1')
	answers = ['wrong', server.PASSWORD]
	prompts = []
	ssh.set_prompts(password=lambda prompt: prompts.append(prompt) or answers.pop(0))
	pynbus2.connect(server.uri('/dev1')).close()
	assert prompts == ['Password of nbus@127.0.0.1:', 'Not accepted, try again. Password of nbus@127.0.0.1:']


def test_password_prompt_cancelled(server, monkeypatch):
	monkeypatch.setenv(ssh.TRUST_NEW_ENV, '1')
	ssh.set_prompts(password=lambda prompt: None)
	with pytest.raises(pynbus2.NbusError, match='cancelled'):
		pynbus2.connect(server.uri('/dev1'))


def encrypted_key(home, passphrase):
	"""A private key encrypted with ``passphrase`` where the SSH client looks for the keys."""
	key = paramiko.ECDSAKey.generate()
	(home / '.ssh').mkdir(exist_ok=True)
	key.write_private_key_file(str(home / '.ssh' / 'id_ecdsa'), password=passphrase)
	return key


def test_password_prompt_with_encrypted_key(server, ssh_home, monkeypatch):
	monkeypatch.setenv(ssh.TRUST_NEW_ENV, '1')
	encrypted_key(ssh_home, 'passphrase')
	prompts = []
	ssh.set_prompts(password=lambda prompt: prompts.append(prompt) or server.PASSWORD)
	pynbus2.connect(server.uri('/dev1')).close()
	assert prompts == ['Password of nbus@127.0.0.1:']


def test_passphrase_prompt(server, ssh_home, monkeypatch):
	monkeypatch.setenv(ssh.TRUST_NEW_ENV, '1')
	server.client_key = encrypted_key(ssh_home, 'passphrase')
	prompts = []
	ssh.set_prompts(password=lambda prompt: prompts.append(prompt) or 'passphrase')
	pynbus2.connect(server.uri('/dev1')).close()
	assert prompts == ['Passphrase of the private key of nbus@127.0.0.1:']
