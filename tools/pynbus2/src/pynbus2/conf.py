# SPDX-License-Identifier: GPL-3.0-or-later
#
# Client of the proto-conf service protocol
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""A client of the configuration tree exported by the firmware ``services/proto-conf`` service, an interface
speaking the ``conf`` protocol.

Every request is a map carrying the command in ``"c"`` and the path of the node in ``"p"``, a list of the node
names from the root (an empty list is the root). The response is a map, on error carrying ``"err"``.

    with pynbus2.connect('rible://nwdaq-hh1/conf') as nbus:
        client = ConfClient(nbus.socket())
        for node in client.children([]):
            print(node['n'])
        client.write(['compensation', 'press', 'c1'], 1.0023)
"""

import logging

import cbor2

from .errors import NbusError

logger = logging.getLogger(__name__)

#: Protocol name advertised by the interfaces exporting the firmware service.
PROTOCOL = 'conf'

# enum conf_type (see services/interfaces/conf.h)
CONF_NONE = 0
CONF_SUBTREE = 1
CONF_F = 2
CONF_U32 = 3
CONF_S32 = 4
CONF_U16 = 5
CONF_S16 = 6
CONF_U8 = 7
CONF_S8 = 8
CONF_B = 9
CONF_BSTR = 10
CONF_STR = 11
CONF_ENUM = 12

TYPE_NAMES = {
	CONF_NONE: 'none',
	CONF_SUBTREE: 'subtree',
	CONF_F: 'f',
	CONF_U32: 'u32',
	CONF_S32: 's32',
	CONF_U16: 'u16',
	CONF_S16: 's16',
	CONF_U8: 'u8',
	CONF_S8: 's8',
	CONF_B: 'bool',
	CONF_BSTR: 'bstr',
	CONF_STR: 'str',
	CONF_ENUM: 'enum',
}

# enum conf_dir
CONF_DIR_NEXT = 0
CONF_DIR_PREV = 1
CONF_DIR_CHILD = 2
CONF_DIR_UP = 3

# enum conf_flag
CONF_READ = (1 << 0)
CONF_WRITE = (1 << 1)
CONF_CONST = (1 << 2)
CONF_STATUS = (1 << 3)
CONF_DETAIL = (1 << 4)
CONF_CREATE = (1 << 5)


class ConfError(NbusError):
	"""A configuration request failed: there is no valid response or the device reported an error."""


def parse_value(type, s):
	"""Parse the text ``s`` as a value of the conf ``type``, e.g. a value given on the command line. Raises
	:class:`ValueError` if it cannot be parsed or the type holds no value."""
	if type == CONF_F:
		return float(s)
	if type in (CONF_U32, CONF_U16, CONF_U8, CONF_ENUM, CONF_S32, CONF_S16, CONF_S8):
		return int(s, 0)
	if type == CONF_B:
		if s.lower() in ('true', '1', 'yes', 'on'):
			return True
		if s.lower() in ('false', '0', 'no', 'off'):
			return False
		raise ValueError(f'{s!r} is not a bool')
	if type == CONF_STR:
		return s
	if type == CONF_BSTR:
		return bytes.fromhex(s)
	raise ValueError(f'node type {TYPE_NAMES.get(type, str(type))!r} holds no value')


class ConfClient:
	"""A client of a conf interface the ``socket`` (a :class:`pynbus2.NbusSocket`) is connected to. The client
	must be the only user of the socket. Paths are lists of node names from the root."""

	#: Number of attempts of a request before giving up.
	RETRIES = 10

	#: How long running the load or save job may take, in seconds.
	JOB_TIMEOUT = 10.0

	def __init__(self, socket):
		self._socket = socket

	def call(self, req, timeout=None, retries=None):
		"""Send the request map ``req`` and return the response map. Raises :class:`ConfError` if there is no
		valid response or it reports an error.

		Without ``timeout`` the retransmit interval suits the transport (the socket's ``request_timeout``).
		A request is sent ``retries`` times at most, :attr:`RETRIES` by default.
		"""
		reply = self._socket.request(cbor2.dumps(req), timeout=timeout or self._socket.request_timeout,
		                             retries=retries or self.RETRIES)
		if reply is None:
			logger.debug('%r: no response', req)
			raise ConfError('no response')
		try:
			r = cbor2.loads(reply)
		except Exception as e:
			logger.debug('%r: undecodable response: %s', req, e)
			raise ConfError('undecodable response')
		logger.debug('%r: %r', req, r)
		if not isinstance(r, dict):
			raise ConfError('response is not a map')
		if r.get('err'):
			raise ConfError(r['err'])
		return r

	def stat(self, path):
		"""The node at ``path`` as a map ``{"n": <name>, "t": <type>, "f": <flags>}``."""
		return self.call({'c': 'stat', 'p': path})

	def read(self, path):
		"""The value of the node at ``path``."""
		return self.call({'c': 'read', 'p': path}).get('val')

	def write(self, path, value, type=None):
		"""Write ``value`` to the node at ``path``. Without ``type`` the type of the node is read first."""
		if type is None:
			type = self.stat(path).get('t', CONF_NONE)
		self.call({'c': 'write', 'p': path, 't': type, 'val': value})

	def walk(self, path, dir, limit=None):
		"""The nodes in the direction ``dir`` (``CONF_DIR_*``) of the node at ``path``, a list of
		``{"n", "t", "f"}`` maps. The device caps the siblings returned at once."""
		req = {'c': 'walk', 'p': path, 'dir': dir}
		if limit is not None:
			req['limit'] = limit
		return self.call(req).get('nodes', [])

	def children(self, path):
		"""All the child nodes of the node at ``path``, a list of ``{"n", "t", "f"}`` maps. The siblings are
		walked again from the last one returned until there are no more."""
		nodes = self.walk(path, CONF_DIR_CHILD)
		while nodes:
			more = self.walk(path + [nodes[-1]['n']], CONF_DIR_NEXT)
			if not more:
				break
			nodes += more
		return nodes

	def load(self):
		"""Run the job loading the stored configuration on the device and wait for it to end."""
		self._run_job('load')

	def save(self):
		"""Run the job saving the configuration on the device and wait for it to end."""
		self._run_job('save')

	def _run_job(self, cmd):
		# The device replies once the job ends, so the request is sent only once with a long timeout, a
		# retransmit would start the job again.
		self.call({'c': cmd}, timeout=self.JOB_TIMEOUT, retries=1)
