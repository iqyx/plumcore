# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 proxy service bridging its stdio to the pynbus2 transports (an SSH login shell)
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""A proxy reaching the nbus2 network of a remote host (e.g. a Linux DAQ controller) over SSH.

The proxy runs on the remote host as the login shell of a dedicated user, so it is started by openssh-server
once the SSH connection is authenticated. It speaks a CBOR protocol on its stdin/stdout and bridges it to the
one pynbus2 transport selected by ``-t``/``--transport`` (e.g. ``-t rible`` or ``-t udp6`` towards the local
nbus2 bridge). Logging goes to stderr, which SSH carries separately.

Setup on the remote host, sshd runs the login shell without arguments, a wrapper script selects the transport::

    printf '#!/bin/sh\\nexec /opt/pynbus2/bin/pynbus2-ssh-proxy -t rible "$@"\\n' > /usr/local/bin/nbus-rible-proxy
    chmod 755 /usr/local/bin/nbus-rible-proxy
    useradd -m -s /usr/local/bin/nbus-rible-proxy nbus
    # add the client public key to ~nbus/.ssh/authorized_keys
    # the shell must be listed in /etc/shells for some PAM configurations

The proxy ignores the ``-c <command>`` sshd passes to the shell when the client requests a command, so
``ssh nbus@daq`` and ``ssh nbus@daq anything`` both start it. Disable everything printing to the session
before the shell starts (motd, last login, banners in shell profiles) or let the client skip it (see below).

Protocol
--------

Both directions carry a sequence of CBOR items (RFC 8742) with no further framing. Every item is a map.

The proxy starts with a greeting wrapped in the self-describe tag 55799, so its first bytes are always
``d9 d9 f7``. A client skips anything preceding them (e.g. a motd printed by sshd)::

    55799({"c": "hello", "p": "nbus2-ssh-proxy", "pv": "1.1.0", "t": <transport scheme>})

Requests carry the command in ``"c"`` and an optional request ID in ``"id"``. Every request is answered with
one response map in the order the requests were received. A response echoes ``"id"`` and carries either the
result fields or ``"err"`` with an error message. It never contains ``"c"``::

    probe       u=<uri prefix>                   {"u": [<uri>, ...]}
    connect     u=<uri>                          {"t": <connection>, "d": [<sid>, <ep>] | null}
    disconnect  t=<connection>                   {}
    walk        t=<connection>                   {"i": [[<name>, <desc>], ...]}
    open        u=<uri>                          {"s": <session>}
    open        t=<connection>, d=[<sid>, <ep>]  {"s": <session>}
    close       s=<session>                      {}
    send        s=<session>, d=<data>            {}

URIs are those of the proxied transport without their ``<scheme>://`` prefix, e.g. ``nwdaq-main-hh1/conf``
for ``rible://nwdaq-main-hh1/conf`` or ``/00000010/1`` for ``udp6:///00000010/1``. ``probe`` refines an
incomplete URI exactly as :func:`pynbus2.probe` does, an empty or missing ``"u"`` probes the bare scheme
(e.g. lists the bonded BLE devices for ``rible``).

``connect`` opens the transport connection a URI names and holds it until ``disconnect``, returning a
connection number and the destination the URI names (or the transport default, e.g. the root interface of a
``rible`` device), ``null`` if there is none. ``walk`` discovers the interfaces of a connection as
:meth:`pynbus2.Nbus.walk` does, each desc is the map describing the interface (``p``, ``pv``, ``mtu``, ``ms``).

``open`` opens a socket and returns a session number, either to the destination a complete URI names, or to
the destination ``d`` (a service ID or an interface name, and an endpoint) on a connection. Any number of
sessions may be open at once. Sessions to URIs differing only in their destinations (e.g.
``udp6:///00000010/1`` and ``udp6:///00000011/1``, or two services behind one serial port) share one
transport connection with each other and with ``connect``, it is closed with the last of them. ``send``
sends one datagram (a byte string) on a session.

Events are sent by the proxy on its own and carry ``"c"`` and no ``"id"``::

    recv    s=<session>, d=<data>   a datagram was received on a session
    closed  s=<session>, err=<msg>  a session failed and was closed

Requests are processed one at a time, a slow ``probe`` or ``open`` delays the responses to the following
requests, but not the ``recv`` events of the sessions already open. Closing stdin closes all sessions and
terminates the proxy.
"""

import argparse
import logging
import sys
import threading

from .nbus import NbusSocket, probe
from .uri import parse_uri
from .transport import build_transport, transport_for, schemes
from .transport.ssh import PROTOCOL, PROTOCOL_VERSION, SELF_DESCRIBE_TAG
from .errors import NbusError

logger = logging.getLogger(__name__)

#: How often a session reader checks it is being closed, in seconds.
_RX_TICK = 0.2


class ProxyError(NbusError):
	"""A request cannot be processed."""


class Connection:
	"""A transport shared by the sessions to the destinations reached over it."""

	def __init__(self, transport):
		self.transport = transport
		self.refs = 0


class Session:
	"""One opened URI: a socket connected to the destination the URI names, on a shared connection."""

	def __init__(self, proxy, number, key, sock):
		self._proxy = proxy
		self.number = number
		self.key = key
		self._sock = sock
		self._stop = threading.Event()
		self._thread = threading.Thread(target=self._rx_loop, name='ssh-proxy-s%d' % number, daemon=True)
		self._thread.start()

	def send(self, data):
		self._sock.send(data)

	def close(self):
		self._stop.set()
		if self._thread is not threading.current_thread():
			self._thread.join()
		self._sock.close()

	def _rx_loop(self):
		while not self._stop.is_set():
			try:
				data = self._sock.recv(_RX_TICK)
			except Exception as e:
				if self._stop.is_set():
					return
				logger.warning('session %d failed: %s', self.number, e)
				self._proxy._event({'c': 'closed', 's': self.number, 'err': str(e) or e.__class__.__name__})
				self._proxy._drop(self.number)
				return
			if data is not None:
				self._proxy._event({'c': 'recv', 's': self.number, 'd': bytes(data)})


class Proxy:
	"""Processes the requests decoded from the client and writes the responses and events to ``tx``."""

	def __init__(self, tx, cbor2, scheme):
		self._tx = tx
		self._cbor = cbor2
		self._scheme = scheme
		self._tx_lock = threading.Lock()
		self._sessions = {}
		self._connections = {}
		# The connections held by connect, by their numbers, as (key, connection).
		self._held = {}
		self._next_held = 1
		# Guards the sessions and the connections, the session readers close the sessions failing.
		self._lock = threading.Lock()
		self._next_session = 1
		self._commands = {
			'probe': self._cmd_probe,
			'connect': self._cmd_connect,
			'disconnect': self._cmd_disconnect,
			'walk': self._cmd_walk,
			'open': self._cmd_open,
			'close': self._cmd_close,
			'send': self._cmd_send,
		}

	def hello(self):
		self._write(self._cbor.CBORTag(SELF_DESCRIBE_TAG, {'c': 'hello', 'p': PROTOCOL, 'pv': PROTOCOL_VERSION,
		                                                    't': self._scheme}))

	def handle(self, req):
		"""Process one request and write its response."""
		try:
			if not isinstance(req, dict):
				raise ProxyError('the request must be a map')
			handler = self._commands.get(req.get('c'))
			if handler is None:
				raise ProxyError('unknown command %r' % (req.get('c'),))
			reply = handler(req)
		except Exception as e:
			logger.debug('request failed: %s', e)
			reply = {'err': str(e) or e.__class__.__name__}
		if isinstance(req, dict) and 'id' in req:
			reply['id'] = req['id']
		self._write(reply)

	def close(self):
		"""Close all the sessions and release the connections held."""
		with self._lock:
			sessions = list(self._sessions.values())
			self._sessions.clear()
			held = list(self._held.values())
			self._held.clear()
		for session in sessions:
			self._close_session(session)
		for key, _ in held:
			self._release(key)

	def _cmd_probe(self, req):
		prefix = '%s://' % self._scheme
		return {'u': [u[len(prefix):] if u.startswith(prefix) else u for u in probe(self._uri(req.get('u')))]}

	def _cmd_connect(self, req):
		uri = self._uri(req.get('u'))
		key, dst, connection = self._acquire(uri)
		try:
			if dst is None:
				dst = connection.transport.default_dst
		except Exception:
			self._release(key)
			raise
		with self._lock:
			number = self._next_held
			self._next_held += 1
			self._held[number] = (key, connection)
		logger.info('connection %d held', number)
		return {'t': number, 'd': list(dst) if dst is not None else None}

	def _cmd_disconnect(self, req):
		with self._lock:
			key, _ = self._held.pop(req.get('t'), (None, None))
		if key is None:
			raise ProxyError('no such connection')
		self._release(key)
		return {}

	def _cmd_walk(self, req):
		_, connection = self._held_connection(req)
		return {'i': [[name, desc] for name, desc in connection.transport.walk()]}

	def _cmd_open(self, req):
		if 't' in req:
			dst = req.get('d')
			if not isinstance(dst, list) or len(dst) != 2:
				raise ProxyError('the destination must be a [sid, ep] array')
			key, connection = self._held_connection(req)
			with self._lock:
				connection.refs += 1
			what = 'connection %d %r' % (req['t'], dst)
		else:
			what = self._uri(req.get('u'))
			key, dst, connection = self._acquire(what)

		try:
			if dst is None:
				dst = connection.transport.default_dst
			if dst is None or dst[1] is None:
				raise ProxyError('the URI does not name a destination and an endpoint')
			sock = NbusSocket(connection.transport.open_socket())
			try:
				sock.connect(*dst)
			except Exception:
				sock.close()
				raise
		except Exception:
			self._release(key)
			raise

		with self._lock:
			number = self._next_session
			self._next_session += 1
			self._sessions[number] = Session(self, number, key, sock)
		logger.info('session %d opened to %s', number, what)
		return {'s': number}

	def _acquire(self, uri):
		"""Take a reference to the connection ``uri`` names, opening it if needed. Returns the connection key,
		the destination the URI names (``None`` if it names none) and the connection."""
		parsed = parse_uri(uri)
		key, dst = transport_for(parsed.scheme).split_uri(parsed)

		with self._lock:
			connection = self._connections.get(key)
			if connection is not None:
				connection.refs += 1
		if connection is None:
			connection = Connection(build_transport(parsed))
			connection.refs = 1
			with self._lock:
				self._connections[key] = connection
			logger.info('connection to %s opened', uri)
		return key, dst, connection

	def _held_connection(self, req):
		with self._lock:
			held = self._held.get(req.get('t'))
		if held is None:
			raise ProxyError('no such connection')
		return held

	def _cmd_close(self, req):
		with self._lock:
			session = self._sessions.pop(req.get('s'), None)
		if session is None:
			raise ProxyError('no such session')
		self._close_session(session)
		logger.info('session %d closed', session.number)
		return {}

	def _cmd_send(self, req):
		data = req.get('d')
		if not isinstance(data, bytes):
			raise ProxyError('the data must be a byte string')
		with self._lock:
			session = self._sessions.get(req.get('s'))
		if session is None:
			raise ProxyError('no such session')
		session.send(data)
		return {}

	def _uri(self, rest):
		"""The full URI of the proxied transport from the part after its ``scheme://``."""
		if rest is None:
			rest = ''
		if not isinstance(rest, str):
			raise ProxyError('the URI must be a string')
		return '%s://%s' % (self._scheme, rest)

	def _drop(self, number):
		"""Close a session which failed on its own."""
		with self._lock:
			session = self._sessions.pop(number, None)
		if session is not None:
			self._close_session(session)

	def _close_session(self, session):
		session.close()
		self._release(session.key)

	def _release(self, key):
		"""Drop a reference to a connection, closing it with the last one."""
		with self._lock:
			connection = self._connections[key]
			connection.refs -= 1
			if connection.refs > 0:
				return
			del self._connections[key]
		connection.transport.close()
		logger.info('connection closed')

	def _event(self, event):
		self._write(event)

	def _write(self, item):
		with self._tx_lock:
			self._tx.write(self._cbor.dumps(item))
			self._tx.flush()


def serve(rx, tx, scheme):
	"""Run the proxy of the transport ``scheme`` on the binary streams ``rx`` and ``tx`` until ``rx`` reaches
	its end."""
	try:
		import cbor2
	except ImportError as e:
		raise ImportError('the ssh proxy requires cbor2; install it with "pip install cbor2" '
		                  'or "pip install pynbus2[proxy]"') from e

	proxy = Proxy(tx, cbor2, scheme)
	proxy.hello()
	decoder = cbor2.CBORDecoder(rx)
	try:
		while True:
			try:
				req = decoder.decode()
			except cbor2.CBORDecodeEOF:
				break
			proxy.handle(req)
	except cbor2.CBORDecodeError as e:
		# The stream has no framing to resynchronize on.
		logger.error('malformed request, terminating: %s', e)
	finally:
		proxy.close()


def main(argv=None):
	parser = argparse.ArgumentParser(description='Bridge a CBOR protocol on stdio to the pynbus2 transports.')
	# sshd runs the login shell as "<shell> -c <command>" when the client requests a command, it is ignored.
	parser.add_argument('-c', dest='command', help=argparse.SUPPRESS)
	parser.add_argument('-t', '--transport', required=True, choices=[s for s in schemes() if s != 'ssh'],
	                    help='the transport scheme to proxy')
	parser.add_argument('--log-level', default='WARNING', help='stderr logging level (default: %(default)s)')
	args = parser.parse_args(argv)

	logging.basicConfig(stream=sys.stderr, level=args.log_level.upper(),
	                    format='%(asctime)s %(name)s %(levelname)s: %(message)s')

	rx = sys.stdin.buffer
	tx = sys.stdout.buffer
	# Anything printed by the transports or their libraries must not corrupt the protocol stream.
	sys.stdout = sys.stderr

	try:
		serve(rx, tx, args.transport)
	except KeyboardInterrupt:
		pass
	except BrokenPipeError:
		logger.info('client disconnected')
	return 0


if __name__ == '__main__':
	sys.exit(main())
