#!/usr/bin/env python3

import sys
import argparse
import cbor2
import socket

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication, QMainWindow, QTreeWidget, QTreeWidgetItem


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


def init_args():
	parser = argparse.ArgumentParser(
		description="plumCore proto-conf over nbus2 configuration tree viewer",
		epilog="(c) 2026 Marek Koza <qyx@krtko.org>",
		formatter_class=argparse.RawDescriptionHelpFormatter
	)

	parser.add_argument('-s', '--sid', type=str, required=True, help='service ID to connect to')
	parser.add_argument('-e', '--ep', type=int, required=True, help='service endpoint to connect to')

	return parser.parse_args()


class NbusClient:

	def __init__(self, sid: str, ep: int, mtu=1024):
		self._sid = sid
		self._ep = ep
		self._mtu = mtu

		self._connect()

	def _connect(self):
		self._s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
		self._s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
		self._s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		self._s.bind((f'fd00:dead:beef::1', 52000))
		self._s.connect((f'fd00:dead:beef::{self._sid[:4]}:{self._sid[4:]}', 52000 + self._ep))
		self._s.settimeout(0.05)

	def call(self, req: dict):
		timeout = 50
		while True:
			timeout -= 1
			if timeout == 0:
				return None

			self._s.send(cbor2.dumps(req));
			try:
				resp = cbor2.loads(self._s.recv(self._mtu))
				return resp
			except TimeoutError:
				continue
			except Exception as e:
				return None

		return None


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
		first = self._walk(path, CONF_DIR_CHILD)
		if not first:
			return []
		return first + self._next_siblings(path + [first[0]['n']])

	def _fmt_value(self, val):
		if isinstance(val, bytes):
			return val.hex()
		return str(val)

	def children(self, path=None):
		"""Return a single level of children of the node at path as a list of dicts:
		{'name', 'type', 'value', 'is_subtree', 'path'}. Leaf values are read here;
		subtrees are not descended into (lazy loading is up to the caller)."""
		path = path or []
		nodes = []
		for ch in self._children(path):
			name = ch.get('n', '')
			type = ch.get('t', CONF_NONE)
			child_path = path + [name]

			value = ''
			is_subtree = (type == CONF_SUBTREE)
			if not is_subtree and type != CONF_NONE:
				val = self._read(child_path)
				if val is not None:
					value = self._fmt_value(val)

			nodes.append({
				'name': name,
				'type': type,
				'value': value,
				'is_subtree': is_subtree,
				'path': child_path,
			})
		return nodes


class ConfTreeWidget(QTreeWidget):

	PATH_ROLE = Qt.UserRole

	def __init__(self, conf: ConfClient):
		super().__init__()
		self._conf = conf

		self.setColumnCount(3)
		self.setHeaderLabels(['Name', 'Type', 'Value'])
		self.itemExpanded.connect(self._on_expanded)

		self.setAlternatingRowColors(True)
		self.setIndentation(22)
		self.setStyleSheet("""
			QTreeWidget {
				outline: 0;
			}
			QTreeView::item {
				padding: 5px 8px;
			}
			QHeaderView::section {
				padding: 6px 8px;
				font-weight: bold;
			}
		""")

		self._add_nodes(self.invisibleRootItem(), conf.children())
		for i in range(3):
			self.resizeColumnToContents(i)

	def _add_nodes(self, parent, nodes):
		for node in nodes:
			item = QTreeWidgetItem(parent, [
				node['name'],
				TYPE_NAMES.get(node['type'], str(node['type'])),
				node['value'],
			])
			item.setData(0, self.PATH_ROLE, node['path'])
			if node['is_subtree']:
				# Add a placeholder child so the expand arrow appears; the real
				# children are loaded lazily when the item is first expanded.
				item.addChild(QTreeWidgetItem(['...']))

	def _on_expanded(self, item):
		# A single child without a stored path is the unexpanded placeholder.
		if item.childCount() == 1 and item.child(0).data(0, self.PATH_ROLE) is None:
			item.takeChildren()
			self._add_nodes(item, self._conf.children(item.data(0, self.PATH_ROLE)))


class MainWindow(QMainWindow):

	def __init__(self, conf: ConfClient):
		super().__init__()
		self.setWindowTitle('plumCore conf tree')
		self.resize(600, 500)
		self.setCentralWidget(ConfTreeWidget(conf))


if __name__ == "__main__":
	args = init_args()

	n = NbusClient(args.sid, args.ep)
	c = ConfClient(n)

	app = QApplication(sys.argv)
	w = MainWindow(c)
	w.show()
	sys.exit(app.exec())
