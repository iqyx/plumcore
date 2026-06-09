#!/usr/bin/env python3

import sys
import os
from colorama import init as colorama_init, Fore, Back, Style
import argparse
import cbor2

# Allow running straight from the source tree without installing pynbus2.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pynbus2', 'src'))
import pynbus2


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


def init_args():
	parser = argparse.ArgumentParser(
		description="plumCore proto-conf over nbus2 configuration tree tool",
		epilog="example: proto-conf.py udp6:///00010002/3 --walk\n\n(c) 2026 Marek Koza <qyx@krtko.org>",
		formatter_class=argparse.RawDescriptionHelpFormatter
	)

	parser.add_argument('uri', type=str, help='nbus2 connection URI carrying the destination, e.g. udp6:///<sid>/<ep>')
	parser.add_argument('-w', '--walk', type=str, nargs='?', const='', default=None, metavar='PATH', help='walk and show the configuration tree at PATH, slash separated (default: root)')
	parser.add_argument('-r', '--read', type=str, default=None, metavar='PATH', help='read the value of the conf node at PATH and print it (script-friendly)')
	parser.add_argument('-v', '--verbose', action='store_true', help='log all nbus2 protocol calls and responses')

	return parser.parse_args()


class NbusClient:
	"""CBOR request/response over a pynbus2 socket, with optional protocol logging."""

	def __init__(self, sock: pynbus2.NbusSocket, verbose=False):
		self._sock = sock
		self._verbose = verbose

	def call(self, req: dict):
		reply = self._sock.request(cbor2.dumps(req), timeout=0.05, retries=50)
		if reply is None:
			self._log(req, None)
			return None
		try:
			resp = cbor2.loads(reply)
		except Exception as e:
			self._log(req, e)
			return None
		self._log(req, resp)
		return resp

	def _log(self, req, resp):
		if not self._verbose:
			return
		if resp is None:
			r = f'{Fore.RED}timeout{Style.RESET_ALL}'
		elif isinstance(resp, Exception):
			r = f'{Fore.RED}{resp!r}{Style.RESET_ALL}'
		else:
			r = repr(resp)
		print(f'{Fore.YELLOW}>>{Style.RESET_ALL} {req!r} {Fore.YELLOW}<<{Style.RESET_ALL} {r}', file=sys.stderr)


class ConfClient:

	def __init__(self, n: NbusClient):
		self._n = n

	def _walk(self, path, dir, limit=None):
		req = {'c': 'walk', 'p': path, 'dir': dir}
		if limit is not None:
			req['limit'] = limit
		r = self._n.call(req)
		if r is None:
			raise ValueError('no response')
		if r.get('err'):
			raise ValueError(r.get('err', 'unknown error'))
		return r.get('nodes', [])

	def _read(self, path):
		r = self._n.call({'c': 'read', 'p': path})
		if r is None or r.get('err'):
			return None
		return r.get('val')

	def _next_siblings(self, path):
		"""Collect all siblings following the node identified by path. The responder
		caps a single walk to MAX_SIBLINGS, so keep walking from the last returned node."""
		result = []
		cur = path
		while True:
			batch = self._walk(cur, CONF_DIR_NEXT)
			if not batch:
				break
			result += batch
			cur = cur[:-1] + [batch[-1]['n']]
		return result

	def _children(self, path):
		"""Return the list of {n,t,f} child nodes of the node identified by path."""
		first = self._walk(path, CONF_DIR_CHILD)
		if not first:
			return []
		return first + self._next_siblings(path + [first[0]['n']])

	def _fmt_value(self, val):
		if isinstance(val, bytes):
			return val.hex()
		return str(val)

	def _print_tree(self, path, prefix):
		children = self._children(path)
		for i, ch in enumerate(children):
			name = ch.get('n', '')
			type = ch.get('t', CONF_NONE)
			flags = ch.get('f', 0)
			last = (i == len(children) - 1)
			connector = '└── ' if last else '├── '
			type_name = TYPE_NAMES.get(type, str(type))

			child_path = path + [name]
			line = f'{prefix}{connector}{Fore.BLUE}{Style.BRIGHT}{name}{Style.RESET_ALL} {Style.DIM}({type_name}){Style.RESET_ALL}'
			if type not in (CONF_NONE, CONF_SUBTREE):
				val = self._read(child_path)
				if val is not None:
					line += f' = {Fore.GREEN}{self._fmt_value(val)}{Style.RESET_ALL}'
			print(line)

			if type == CONF_SUBTREE:
				ext = '    ' if last else '│   '
				self._print_tree(child_path, prefix + ext)

	def walk(self, path=None):
		path = path or []
		root = '/'.join(path) if path else 'root'
		print(f'{Fore.BLUE}{Style.BRIGHT}{root}{Style.RESET_ALL}')
		self._print_tree(path, '')

	def read(self, path=None):
		"""Read the single conf node at the path and print its raw value (script-friendly)."""
		path = path or []
		val = self._read(path)
		if val is None:
			print(f'cannot read {"/".join(path) if path else "root"}', file=sys.stderr)
			sys.exit(1)
		print(self._fmt_value(val))


if __name__ == "__main__":

	colorama_init()
	args = init_args()

	with pynbus2.connect(args.uri) as nbus:
		sock = nbus.socket()
		if not sock._connected:
			print(f'the URI must carry a destination, e.g. udp6:///<sid>/<ep>', file=sys.stderr)
			sys.exit(1)
		n = NbusClient(sock, verbose=args.verbose)
		c = ConfClient(n)

		if args.walk is not None:
			c.walk([p for p in args.walk.split('/') if p])
		if args.read is not None:
			c.read([p for p in args.read.split('/') if p])
