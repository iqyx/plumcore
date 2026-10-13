# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 transport tunnelled over SSH to a remote pynbus2-ssh-proxy
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""The ``ssh`` transport: reach the transports of a remote host through its ``pynbus2-ssh-proxy``.

The OpenSSH client is started as a subprocess, the proxy is the login shell of the remote user (see
:mod:`pynbus2.ssh_proxy` for the setup and the protocol). The proxy serves a single transport chosen when it
is started, so the remote URI carries no scheme. Every socket is a proxy session to a remote URI, the sockets
of a transport share the one SSH connection.

URI form::

    ssh://[<user>@]<host>[:<port>][/<remote URI after its scheme://>][?identity=<key file>&command=<cmd>&...]

    ssh://daq//00000010/1                       udp6:///00000010/1 (a proxy started with -t udp6)
    ssh://nbus@10.0.0.1:23/nwdaq-main-hh1/conf  rible://nwdaq-main-hh1/conf (-t rible)
    ssh://daq//dev/ttyUSB0/00010002/3?baudrate=1000000

The path without its leading ``/`` is the remote URI after its ``scheme://``. A remote URI with an empty host
therefore starts with ``//`` (``ssh://daq//00000010/1`` is ``udp6:///00000010/1``). The query is passed to the
remote URI, except ``identity`` (the private key file, ``ssh -i``) and ``command`` (a remote command, needed
only when the proxy is not the login shell of the remote user). Other SSH options are taken from
``~/.ssh/config``.

Without an explicit destination a socket connects to the remote URI. ``nbus.socket(<sid>, <ep>)`` appends
``/<sid>/<ep>`` to its path instead.
"""

import itertools
import logging
import os
import queue
import subprocess
import threading
from urllib.parse import urlencode

from .base import Transport, SocketBackend, connection_key
from ..addressing import parse_id, parse_ep, format_id
from ..errors import NbusError, UriError

logger = logging.getLogger(__name__)

#: Protocol name and version of the proxy (pynbus2-ssh-proxy).
PROTOCOL = 'nbus2-ssh-proxy'
PROTOCOL_VERSION = '1.0.0'

#: CBOR self-describe tag, the magic preceding the proxy greeting. Its encoding is the first bytes it sends.
SELF_DESCRIBE_TAG = 55799
SELF_DESCRIBE_MAGIC = b'\xd9\xd9\xf7'

#: Query parameters consumed by the ssh transport, not passed to the remote URI.
SSH_PARAMS = ('identity', 'command')

#: Largest amount of output (motd, banners) skipped before the proxy greeting.
_GREETING_MAX_SKIP = 65536

#: How long a request may take, probing and opening (BLE scans, connecting a device) can take seconds.
_REQUEST_TIMEOUT = 30.0


def _import_cbor2():
	try:
		import cbor2
	except ImportError as e:
		raise ImportError('the ssh transport requires cbor2; install it with "pip install cbor2" '
		                  'or "pip install pynbus2[ssh]"') from e
	return cbor2


def _netloc(uri):
	host = '[%s]' % uri.host if ':' in uri.host else uri.host
	return '%s%s%s' % (uri.username + '@' if uri.username else '', host, ':%d' % uri.port if uri.port else '')


class _Remote(str):
	"""A remote URI given as a socket destination, told apart from a service ID."""


def _remote_parts(uri):
	"""Split an ssh URI into the remote URI after its ``scheme://`` without the query, and the query string."""
	rest = uri.path[1:] if uri.path.startswith('/') else uri.path
	query = urlencode([(k, v) for k, values in uri.query.items() if k not in SSH_PARAMS for v in values])
	return rest, query


def _join_remote(rest, query):
	return _Remote('%s%s' % (rest, '?' + query if query else ''))


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

		cmd = ['ssh', '-T', '-e', 'none']
		if uri.port:
			cmd += ['-p', str(uri.port)]
		if uri.username:
			cmd += ['-l', uri.username]
		if uri.get('identity'):
			cmd += ['-i', uri.get('identity')]
		cmd += ['--', uri.host]
		if uri.get('command'):
			cmd += [uri.get('command')]
		logger.info('running %s', ' '.join(cmd))
		self._proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

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
		if transport._remote[0]:
			transport.default_dst = (_join_remote(*transport._remote), 0)
		return transport

	@classmethod
	def split_uri(cls, uri):
		# The path is the remote URI, it is the destination.
		rest, query = _remote_parts(uri)
		dst = (_join_remote(rest, query), 0) if rest else None
		return connection_key(uri), dst

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

	def remote_uri(self, id, ep):
		"""The remote URI (after its ``scheme://``) a socket connected to (id, ep) opens."""
		if isinstance(id, _Remote):
			return str(id)
		rest, query = self._remote
		return str(_join_remote('%s/%s/%d' % (rest, format_id(parse_id(id)), parse_ep(ep)), query))

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

	def open_session(self, remote_uri):
		number = self.request({'c': 'open', 'u': remote_uri})['s']
		# The session queue may exist already, if a datagram arrived before the response.
		with self._lock:
			q = self._sessions.setdefault(number, queue.Queue())
		logger.info('session %d opened to %s', number, remote_uri)
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
			self._proc.stdin.close()
		except OSError:
			pass
		try:
			self._proc.wait(timeout=5.0)
		except subprocess.TimeoutExpired:
			self._proc.terminate()
			self._proc.wait()

	def _write(self, item):
		with self._write_lock:
			try:
				self._proc.stdin.write(self._cbor.dumps(item))
				self._proc.stdin.flush()
			except (OSError, ValueError) as e:
				raise NbusError('the ssh connection is closed') from e

	def _greeting(self):
		"""Skip any output preceding the proxy greeting and check it."""
		stdout = self._proc.stdout
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
		if str(hello.get('pv', '')).split('.')[0] != PROTOCOL_VERSION.split('.')[0]:
			raise NbusError('unsupported proxy protocol version %s' % hello.get('pv'))
		logger.info('connected to %s %s', hello['p'], hello['pv'])

	def _rx_loop(self):
		decoder = self._cbor.CBORDecoder(self._proc.stdout)
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
		for line in self._proc.stderr:
			logger.warning('ssh: %s', line.decode(errors='replace').rstrip())


class SshSocket(SocketBackend):
	"""A proxy session to a remote URI."""

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
		self._session, self._queue = self._transport.open_session(self._transport.remote_uri(id, ep))

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
