# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 transport tunnelled over SSH to a remote pynbus2-ssh-proxy
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""The ``ssh`` transport: reach the transports of a remote host through its ``pynbus2-ssh-proxy``.

The SSH connection is made with paramiko, the proxy is the login shell of the remote user (see
:mod:`pynbus2.ssh_proxy` for the setup and the protocol). The proxy serves a single transport chosen when it
is started, so the remote URI carries no scheme. The transport holds the remote transport connection the
remote URI names, every socket is a proxy session to a destination on it, sharing the one SSH connection.

URI form::

    ssh://[<user>@]<host>[:<port>][/<remote URI after its scheme://>][?identity=<key file>&command=<cmd>&...]

    ssh://daq//00000010/1                       udp6:///00000010/1 (a proxy started with -t udp6)
    ssh://nbus@10.0.0.1:23/nwdaq-main-hh1/conf  rible://nwdaq-main-hh1/conf (-t rible)
    ssh://daq//dev/ttyUSB0/00010002/3?baudrate=1000000

The path without its leading ``/`` is the remote URI after its ``scheme://``. A remote URI with an empty host
therefore starts with ``//`` (``ssh://daq//00000010/1`` is ``udp6:///00000010/1``). The query is passed to the
remote URI, except ``identity`` (the private key file, ``ssh -i``) and ``command`` (a remote command, needed
only when the proxy is not the login shell of the remote user).

The host name, user, port and identity files of a host alias are taken from ``~/.ssh/config``, the keys of the
SSH agent and those in ``~/.ssh`` are tried. The host key must be in ``~/.ssh/known_hosts``. An unknown one is
trusted (and added there) when ``PYNBUS2_SSH_TRUST_NEW=1`` is set or the host key prompt accepts it, a changed
one is always refused. A password (or the passphrase of a private key) is taken from ``PYNBUS2_SSH_PASSWORD``
or asked by the password prompt. Applications install the prompts with :func:`set_prompts`.

Without an explicit destination a socket connects to the destination the remote URI names, or the default
destination of the remote transport (e.g. the root interface of a ``rible`` device). ``nbus.socket(<sid>,
<ep>)`` connects to the service (or the interface name) ``<sid>`` on the remote connection instead, and
``nbus.walk()`` discovers its interfaces, as if the remote URI was connected directly.
"""

import base64
import getpass
import hashlib
import itertools
import logging
import os
import queue
import threading
from urllib.parse import urlencode

from .base import Transport, SocketBackend, connection_key
from ..errors import NbusError, UriError

logger = logging.getLogger(__name__)

#: Protocol name and version of the proxy (pynbus2-ssh-proxy).
PROTOCOL = 'nbus2-ssh-proxy'
PROTOCOL_VERSION = '1.1.0'

#: CBOR self-describe tag, the magic preceding the proxy greeting. Its encoding is the first bytes it sends.
SELF_DESCRIBE_TAG = 55799
SELF_DESCRIBE_MAGIC = b'\xd9\xd9\xf7'

#: Query parameters consumed by the ssh transport, not passed to the remote URI.
SSH_PARAMS = ('identity', 'command')

#: Largest amount of output (motd, banners) skipped before the proxy greeting.
_GREETING_MAX_SKIP = 65536

#: How long a request may take, probing and opening (BLE scans, connecting a device) can take seconds.
_REQUEST_TIMEOUT = 30.0

#: Environment variable with the password, or the passphrase of a private key, used when one is needed.
PASSWORD_ENV = 'PYNBUS2_SSH_PASSWORD'

#: Environment variable trusting unknown host keys (on first use, they are added to known_hosts) when set to 1.
TRUST_NEW_ENV = 'PYNBUS2_SSH_TRUST_NEW'

KNOWN_HOSTS = '~/.ssh/known_hosts'
SSH_CONFIG = '~/.ssh/config'

_CONNECT_TIMEOUT = 10.0

#: How many times a password is asked for again when it is not accepted.
_PASSWORD_RETRIES = 2

_password_prompt = None
_host_key_prompt = None


def set_prompts(password=None, host_key=None):
	"""Install the callbacks asking the user, they are called from the thread connecting.

	``password(prompt)`` returns the password or passphrase ``prompt`` asks for, ``None`` cancels connecting.
	``host_key(host, key_type, fingerprint)`` returns whether to trust the unknown host key, it is added to
	known_hosts then. Without a callback the environment is used, see :data:`PASSWORD_ENV` and
	:data:`TRUST_NEW_ENV`.
	"""
	global _password_prompt, _host_key_prompt
	_password_prompt = password
	_host_key_prompt = host_key


def _import_cbor2():
	try:
		import cbor2
	except ImportError as e:
		raise ImportError('the ssh transport requires cbor2; install it with "pip install cbor2" '
		                  'or "pip install pynbus2[ssh]"') from e
	return cbor2


def _import_paramiko():
	try:
		import paramiko
	except ImportError as e:
		raise ImportError('the ssh transport requires paramiko; install it with "pip install paramiko" '
		                  'or "pip install pynbus2[ssh]"') from e
	return paramiko


def fingerprint(key):
	"""The SHA256 fingerprint of a paramiko key, as OpenSSH shows it."""
	return 'SHA256:' + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip('=')


def _remember_host_key(paramiko, host, key):
	path = os.path.expanduser(KNOWN_HOSTS)
	try:
		os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
		with open(path, 'a') as f:
			f.write(paramiko.hostkeys.HostKeyEntry([host], key).to_line())
	except OSError as e:
		logger.warning('cannot add the host key of %s to %s: %s', host, path, e)


def _host_key_policy(paramiko):
	class Policy(paramiko.MissingHostKeyPolicy):
		"""Trusts an unknown host key if the environment or the host key prompt says so."""

		def missing_host_key(self, client, host, key):
			fp = fingerprint(key)
			if os.environ.get(TRUST_NEW_ENV) == '1':
				logger.warning('trusting the unknown %s host key %s of %s', key.get_name(), fp, host)
			elif _host_key_prompt is None or not _host_key_prompt(host, key.get_name(), fp):
				raise NbusError('the %s host key %s of %s is not known (set %s=1 to trust it)'
				                % (key.get_name(), fp, host, TRUST_NEW_ENV))
			client.get_host_keys().add(host, key.get_name(), key)
			_remember_host_key(paramiko, host, key)

	return Policy()


def _ssh_config(paramiko, host):
	path = os.path.expanduser(SSH_CONFIG)
	if not os.path.exists(path):
		return {}
	try:
		return paramiko.SSHConfig.from_path(path).lookup(host)
	except Exception as e:
		logger.warning('cannot read %s: %s', path, e)
		return {}


def _secret_needed(paramiko, e, password):
	"""Whether the authentication failure ``e`` may be fixed by a password or a passphrase."""
	if isinstance(e, paramiko.BadAuthenticationType):
		return password is None and bool({'password', 'keyboard-interactive'} & set(e.allowed_types))
	# paramiko raises a plain SSHException when nothing (no key, no password) could be tried.
	return isinstance(e, paramiko.AuthenticationException) or str(e) == 'No authentication methods available'


def _password_allowed(paramiko, client, user):
	"""Whether the server of a client which failed to authenticate accepts passwords, asked on its connection.
	``None`` when unknown."""
	try:
		client.get_transport().auth_none(user)
	except paramiko.BadAuthenticationType as e:
		return bool({'password', 'keyboard-interactive'} & set(e.allowed_types))
	except Exception:
		pass
	return None


def _ssh_connect(uri):
	"""Connect and authenticate the SSH client to the host ``uri`` names. Returns the paramiko SSHClient."""
	paramiko = _import_paramiko()
	config = _ssh_config(paramiko, uri.host)
	identities = [uri.get('identity')] if uri.get('identity') else config.get('identityfile', [])
	identities = [os.path.expanduser(p) for p in identities if os.path.exists(os.path.expanduser(p))]
	host = config.get('hostname', uri.host)
	user = uri.username or config.get('user') or getpass.getuser()
	args = {
		'hostname': host,
		'port': uri.port or int(config.get('port', 22)),
		'username': user,
		'key_filename': identities or None,
		'timeout': _CONNECT_TIMEOUT,
	}

	password = os.environ.get(PASSWORD_ENV)
	for attempt in range(_PASSWORD_RETRIES + 1):
		client = paramiko.SSHClient()
		client.load_system_host_keys()
		client.set_missing_host_key_policy(_host_key_policy(paramiko))
		try:
			client.connect(password=password, **args)
			return client
		except NbusError:
			client.close()
			raise
		except paramiko.SSHException as e:
			# An encrypted private key makes paramiko ask for its passphrase, even if the server takes a
			# password. The password is tried as the passphrase too.
			passphrase = (isinstance(e, paramiko.PasswordRequiredException)
			              and _password_allowed(paramiko, client, user) is False)
			client.close()
			if not _secret_needed(paramiko, e, password):
				raise NbusError('ssh authentication of %s@%s failed: %s' % (user, host, e))
			if _password_prompt is None:
				raise NbusError('ssh authentication of %s@%s failed, set %s to the password (%s)'
				                % (user, host, PASSWORD_ENV, e))
			if passphrase:
				prompt = 'Passphrase of the private key of %s@%s:' % (user, host)
			else:
				prompt = 'Password of %s@%s:' % (user, host)
			if password is not None:
				prompt = 'Not accepted, try again. ' + prompt
			password = _password_prompt(prompt)
			if password is None:
				raise NbusError('ssh authentication of %s@%s cancelled' % (user, host))
		except OSError as e:
			client.close()
			raise NbusError('ssh connection to %s failed: %s' % (host, e))
	raise NbusError('ssh authentication of %s@%s failed' % (user, host))


def _netloc(uri):
	host = '[%s]' % uri.host if ':' in uri.host else uri.host
	return '%s%s%s' % (uri.username + '@' if uri.username else '', host, ':%d' % uri.port if uri.port else '')


def _remote_parts(uri):
	"""Split an ssh URI into the remote URI after its ``scheme://`` without the query, and the query string."""
	rest = uri.path[1:] if uri.path.startswith('/') else uri.path
	query = urlencode([(k, v) for k, values in uri.query.items() if k not in SSH_PARAMS for v in values])
	return rest, query


def _join_remote(rest, query):
	return '%s%s' % (rest, '?' + query if query else '')


def _config_hosts():
	"""The host aliases in ``~/.ssh/config``, without patterns."""
	try:
		with open(os.path.expanduser('~/.ssh/config')) as f:
			lines = f.readlines()
	except OSError:
		return []
	hosts = []
	for line in lines:
		words = line.split()
		if len(words) >= 2 and words[0].lower() == 'host':
			hosts += [w for w in words[1:] if not any(c in w for c in '*?!')]
	return hosts


class SshTransport(Transport):

	def __init__(self, uri, cbor2):
		self._cbor = cbor2
		self._remote = _remote_parts(uri)

		self._write_lock = threading.Lock()
		self._lock = threading.Lock()
		self._ids = itertools.count(1)
		self._pending = {}
		self._sessions = {}
		self._closed = False
		# The number of the remote transport connection held by the proxy, connected on its first use.
		self._connection = None
		self._connection_lock = threading.Lock()

		self._rx_thread = None
		self._client = _ssh_connect(uri)
		try:
			self._channel = self._client.get_transport().open_session()
			# The proxy is the login shell, a shell session without a terminal starts it.
			if uri.get('command'):
				self._channel.exec_command(uri.get('command'))
			else:
				self._channel.invoke_shell()
			self._stdout = self._channel.makefile('rb')
			self._stderr = self._channel.makefile_stderr('rb')
		except Exception as e:
			self._client.close()
			raise NbusError('starting the ssh session failed: %s' % e)

		threading.Thread(target=self._stderr_loop, name='pynbus2-ssh-stderr', daemon=True).start()
		try:
			self._greeting()
		except Exception:
			self.close()
			raise
		self._rx_thread = threading.Thread(target=self._rx_loop, name='pynbus2-ssh-rx', daemon=True)
		self._rx_thread.start()

	@classmethod
	def from_uri(cls, uri):
		if not uri.host:
			raise UriError('the ssh URI must name a host, e.g. ssh://<host>//<sid>/<ep>')
		transport = cls(uri, _import_cbor2())
		# A remote URI without a host (e.g. udp6://) is connected when a socket names the destination.
		if transport._remote[0]:
			try:
				transport.remote_connection()
			except Exception:
				transport.close()
				raise
		return transport

	@classmethod
	def split_uri(cls, uri):
		# Only the proxied transport knows which part of the remote URI is the destination.
		return connection_key(uri, _join_remote(*_remote_parts(uri))), None

	@classmethod
	def probe(cls, uri):
		"""Enumerate the hosts, then the remote URIs probed by the proxy.

		Probed with just the scheme it lists the host aliases from ``~/.ssh/config``. Probed with a host it
		connects the proxy and probes the remote URI (the bare scheme of the proxied transport when there is
		none), the results are returned as ssh URIs.
		"""
		if not uri.host:
			return ['ssh://%s' % host for host in _config_hosts()]

		transport = cls(uri, _import_cbor2())
		try:
			found = transport.request({'c': 'probe', 'u': str(_join_remote(*transport._remote))})['u']
		finally:
			transport.close()

		base = 'ssh://%s' % _netloc(uri)
		ssh_query = [(k, v) for k, values in uri.query.items() if k in SSH_PARAMS for v in values]
		uris = []
		for remote in found:
			rest, _, query = remote.partition('?')
			query = '&'.join(q for q in (urlencode(ssh_query), query) if q)
			uris.append('%s%s%s' % (base, '/' + rest if rest else '', '?' + query if query else ''))
		return uris

	def remote_connection(self):
		"""The number of the remote transport connection held by the proxy, connecting it on the first call.
		Sets the default destination to the one the remote URI names, or the default of the remote transport."""
		with self._connection_lock:
			if self._connection is None:
				resp = self.request({'c': 'connect', 'u': _join_remote(*self._remote)})
				self._connection = resp['t']
				if resp.get('d') is not None:
					self.default_dst = tuple(resp['d'])
				logger.info('remote connection %d, default destination %r', self._connection, self.default_dst)
			return self._connection

	def walk(self):
		"""The interfaces the remote transport connection discovers, a list of (name, desc) tuples."""
		return [(name, desc) for name, desc in self.request({'c': 'walk', 't': self.remote_connection()})['i']]

	def request(self, req, timeout=_REQUEST_TIMEOUT):
		"""Send a request to the proxy and return its response map. Raises NbusError on an error response."""
		slot = queue.Queue(1)
		with self._lock:
			if self._closed:
				raise NbusError('the ssh connection is closed')
			req['id'] = next(self._ids)
			self._pending[req['id']] = slot
		try:
			self._write(req)
			resp = slot.get(timeout=timeout)
		except queue.Empty:
			raise NbusError('no response to the proxy request %r' % req['c'])
		finally:
			with self._lock:
				self._pending.pop(req['id'], None)
		if resp is None:
			raise NbusError('the ssh connection is closed')
		if 'err' in resp:
			raise NbusError('proxy request %r failed: %s' % (req['c'], resp['err']))
		return resp

	def send(self, session, data):
		# Not waiting for the response, a failure is only logged when it arrives.
		self._write({'c': 'send', 's': session, 'd': data})

	def open_session(self, id, ep):
		"""Open a session to the destination (id, ep) on the remote connection, id is a service ID or an
		interface name."""
		number = self.request({'c': 'open', 't': self.remote_connection(), 'd': [id, ep]})['s']
		# The session queue may exist already, if a datagram arrived before the response.
		with self._lock:
			q = self._sessions.setdefault(number, queue.Queue())
		logger.info('session %d opened to %r', number, (id, ep))
		return number, q

	def close_session(self, number):
		with self._lock:
			self._sessions.pop(number, None)
		self.request({'c': 'close', 's': number})

	def open_socket(self):
		return SshSocket(self)

	def close(self):
		with self._lock:
			self._closed = True
		try:
			# The proxy closes its sessions and terminates at the end of its stdin.
			self._channel.shutdown_write()
		except Exception:
			pass
		if self._rx_thread is not None and self._rx_thread is not threading.current_thread():
			self._rx_thread.join(5.0)
		self._client.close()

	def _write(self, item):
		with self._write_lock:
			try:
				self._channel.sendall(self._cbor.dumps(item))
			except Exception as e:
				raise NbusError('the ssh connection is closed') from e

	def _greeting(self):
		"""Skip any output preceding the proxy greeting and check it."""
		stdout = self._stdout
		window = b''
		for _ in range(_GREETING_MAX_SKIP):
			byte = stdout.read(1)
			if not byte:
				raise NbusError('the ssh connection closed before the proxy greeting (see the ssh errors logged)')
			window = (window + byte)[-len(SELF_DESCRIBE_MAGIC):]
			if window == SELF_DESCRIBE_MAGIC:
				break
		else:
			raise NbusError('no proxy greeting received, is the proxy the login shell of the remote user?')

		hello = self._cbor.CBORDecoder(stdout).decode()
		if not isinstance(hello, dict) or hello.get('c') != 'hello' or hello.get('p') != PROTOCOL:
			raise NbusError('unexpected proxy greeting %r' % (hello,))
		try:
			version = tuple(int(n) for n in str(hello.get('pv')).split('.')[:2])
		except ValueError:
			raise NbusError('unsupported proxy protocol version %s' % hello.get('pv'))
		major, minor = (int(n) for n in PROTOCOL_VERSION.split('.')[:2])
		if version[0] != major or version[1:] < (minor,):
			raise NbusError('unsupported proxy protocol version %s, %d.%d needed, update pynbus2 on the remote host'
			                % (hello.get('pv'), major, minor))
		logger.info('connected to %s %s', hello['p'], hello['pv'])

	def _rx_loop(self):
		decoder = self._cbor.CBORDecoder(self._stdout)
		try:
			while True:
				self._dispatch(decoder.decode())
		except Exception as e:
			if not isinstance(e, self._cbor.CBORDecodeEOF):
				logger.error('malformed data from the proxy: %s', e)
		finally:
			with self._lock:
				self._closed = True
				pending = list(self._pending.values())
				sessions = list(self._sessions.values())
			for slot in pending:
				slot.put(None)
			for q in sessions:
				q.put(None)
			logger.info('the ssh connection was closed')

	def _dispatch(self, item):
		if not isinstance(item, dict):
			return
		event = item.get('c')
		if event == 'recv':
			with self._lock:
				q = self._sessions.setdefault(item.get('s'), queue.Queue())
			q.put(bytes(item.get('d', b'')))
		elif event == 'closed':
			logger.warning('session %s closed by the proxy: %s', item.get('s'), item.get('err'))
			with self._lock:
				q = self._sessions.get(item.get('s'))
			if q is not None:
				q.put(None)
		elif event is None:
			if 'id' not in item:
				# A response to a send.
				if 'err' in item:
					logger.warning('sending a datagram failed: %s', item['err'])
				return
			with self._lock:
				slot = self._pending.get(item['id'])
			if slot is not None:
				slot.put(item)

	def _stderr_loop(self):
		for line in self._stderr:
			logger.warning('ssh: %s', line.decode(errors='replace').rstrip())


class SshSocket(SocketBackend):
	"""A proxy session to a destination on the remote connection."""

	# A round trip over the SSH connection and the remote transport.
	request_timeout = 1.00

	def __init__(self, transport):
		self._transport = transport
		self._session = None
		self._queue = None
		self._closed = False

	def bind(self, id, ep):
		pass

	def connect(self, id, ep):
		self._session, self._queue = self._transport.open_session(id, ep)

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
		if self._session is None:
			raise OSError('socket is not connected to a destination')
		if self._closed:
			raise NbusError('session %d was closed' % self._session)
		self._transport.send(self._session, bytes(data))

	def recv(self, timeout):
		if self._session is None:
			raise OSError('socket is not connected to a destination')
		if self._closed:
			return None
		try:
			msg = self._queue.get(timeout=timeout) if timeout is not None else self._queue.get()
		except queue.Empty:
			return None
		if msg is None:
			self._closed = True
		return msg

	def close(self):
		if self._session is None:
			return
		try:
			self._transport.close_session(self._session)
		except Exception as e:
			logger.debug('closing session %d failed: %s', self._session, e)
		self._session = None

	def __repr__(self):
		return '<SshSocket session %s>' % self._session
