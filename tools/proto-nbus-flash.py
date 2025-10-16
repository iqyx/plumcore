#!/usr/bin/env python3

import sys
from colorama import init as colorama_init, Fore, Back, Style
import argparse
import os
import cbor2
import socket
import time
from datetime import datetime, timedelta
import pytz
from tqdm import tqdm


def init_args():
	parser = argparse.ArgumentParser(
		description="plumCore proto-flash over nbus2 upload/download tool",
		epilog="(c) 2025 Marek Koza <qyx@krtko.org>",
		formatter_class=argparse.RawDescriptionHelpFormatter
	)

	parser.add_argument('-l', '--list', action='store_true', help='list flash partitions')
	parser.add_argument('-r', '--reset', action='store_true', help='reset the device')
	parser.add_argument('-s', '--sid', type=str, required=True, help='service ID to connect to')
	parser.add_argument('-e', '--ep', type=int, required=True, help='service endpoint to connect to')
	parser.add_argument('-d', '--download', type=str, help='download content of the flash volume')
	parser.add_argument('-u', '--upload', type=str, help='upload content to the flash volume')
	parser.add_argument('-f', '--file', type=str, default='file.bin', help='name of the file to read from/write to')

	return parser.parse_args()


class NbusClient:

	def __init__(self, sid: str, ep: int, mtu=1024):
		self._sid = sid
		self._ep = ep
		self._mtu = mtu

		self._connect()

	def _connect(self):
		self._s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
		# self._s.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, 'nbus'.encode())
		self._s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
		self._s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		self._s.bind((f'fd00:dead:beef::1', 52000))
		self._s.connect((f'fd00:dead:beef::{self._sid[:4]}:{self._sid[4:]}', 52000 + self._ep))
		self._s.settimeout(0.1)

	def call(self, req: dict):
		self._s.send(cbor2.dumps(req));
		try:
			resp = cbor2.loads(self._s.recv(self._mtu))
		except TimeoutError:
			return None
		except Exception as e:
			return None

		# meh, required
		time.sleep(0.01)

		return resp

class FlashClient:

	def __init__(self, n: NbusClient):
		self._n = n

	def info(self, vol):
		r = self._n.call({'c': 'info', 'n': vol})
		if r.get('err'):
			raise ValueError(r.get('err', 'unknown error'))
		return r

	def _strSizes(self, sizes):
		s = 'volume = %d KB, ' % (sizes[0].get('s') // 1024);
		for i in range(len(sizes) - 2):
			s += 'erase block: %d KB, ' % (sizes[i + 1].get('s') // 1024)
		s += 'page: %d B' % (sizes[-1].get('s'))
		return s

	def list(self):
		vols = self._n.call({'c': 'list'}).get('d', {})
		for vol in vols:
			i = self.info(vol['n'])

			print(f'Volume {Fore.BLUE}{Style.BRIGHT}{vol["n"]}{Style.RESET_ALL}: ' + self._strSizes(i.get('sizes', [])))

	def _open(self, vol):
		r = self._n.call({'c': 'open', 'n': vol})
		self._sid = r.get('sid', None)
		if not self._sid:
			raise ValueError(r.get('err', 'unknown error'))

	def _close(self):
		r = self._n.call({'c': 'close'})

	def _read(self, addr, data_len):
		r = self._n.call({'c': 'read', 'sid': self._sid, 'addr': addr, 'len': data_len})
		return r.get('d', b'')

	def _erase(self, addr, data_len):
		r = self._n.call({'c': 'erase', 'sid': self._sid, 'addr': addr, 'len': data_len})
		if r.get('err'):
			raise ValueError(r.get('err', 'unknown error'))

	def _write(self, addr, data):
		r = self._n.call({'c': 'write', 'sid': self._sid, 'addr': addr, 'len': len(data), 'd': data})
		if r.get('err'):
			raise ValueError(r.get('err', 'unknown error'))

	def reset(self):
		r = self._n.call({'c': 'reset'})

	def download(self, vol, fname):
		r = self.info(vol)
		flash_size = r.get('sizes')[0].get('s')
		page_size = 256

		self._open(vol)
		d = b''
		with tqdm(desc='Download', total=flash_size, ncols=120, unit='B', unit_scale=True, unit_divisor=1024) as progress_bar:
			for i in range(0, flash_size, page_size):
				r = self._read(i, page_size)
				# print(r)
				d += r
				progress_bar.update(page_size)

		self._close()
		with open(fname, 'wb') as f:
			f.write(d)

	def upload(self, vol, fname):
		r = self.info(vol)
		flash_size = r.get('sizes')[0].get('s')
		erase_size = r.get('sizes')[1].get('s')
		page_size = 256

		self._open(vol)
		with tqdm(desc='Erase', total=flash_size, ncols=120, unit='B', unit_scale=True, unit_divisor=1024) as progress_bar:
			for i in range(0, flash_size, erase_size):
				self._erase(i, erase_size)
				progress_bar.update(erase_size)

		with open(fname, 'rb') as f:
			d = f.read()

		# pad the input file
		while len(d) % page_size:
			d += b'\xff'

		with tqdm(desc='Upload', total=len(d), ncols=120, unit='B', unit_scale=True, unit_divisor=1024) as progress_bar:
			for i in range(0, len(d), page_size):
				self._write(i,d[i:(i + page_size)])
				progress_bar.update(page_size)

		self._close()




if __name__ == "__main__":

	colorama_init()
	args = init_args()

	n = NbusClient(args.sid, args.ep)
	f = FlashClient(n)

	if args.list:
		f.list()
	if args.download:
		f.download(args.download, args.file)
	if args.upload:
		f.upload(args.upload, args.file)
	if args.reset:
		f.reset()
