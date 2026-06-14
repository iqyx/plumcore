#!/usr/bin/env python3

import sys
import os
from colorama import init as colorama_init, Fore, Back, Style
import argparse
import cbor2
from tqdm import tqdm

# Allow running straight from the source tree without installing pynbus2.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pynbus2', 'src'))
import pynbus2


# Default flash transfer block size. The device caps a flash read/write block at NBUS_FLASH_BLOCK_LEN
# (256 B), but the limiting factor is the transport: the proto-dgtext serial console link caps a whole
# datagram at 256 B, which has to hold the 24-byte nbus2 header plus the CBOR-framed block. 128 B keeps
# both read responses and write requests safely under that limit and works on every transport.
DEFAULT_BLOCK_SIZE = 128


def init_args():
	parser = argparse.ArgumentParser(
		description="plumCore proto-flash over nbus2 upload/download tool",
		epilog="example: proto-nbus-flash.py udp6:///00010002/1 --list\n\n(c) 2025 Marek Koza <qyx@krtko.org>",
		formatter_class=argparse.RawDescriptionHelpFormatter
	)

	parser.add_argument('uri', type=str, help='nbus2 connection URI carrying the destination, e.g. udp6:///<sid>/<ep>')
	parser.add_argument('-l', '--list', action='store_true', help='list flash partitions')
	parser.add_argument('-r', '--reset', action='store_true', help='reset the device')
	parser.add_argument('-d', '--download', type=str, help='download content of the flash volume')
	parser.add_argument('-u', '--upload', type=str, help='upload content to the flash volume')
	parser.add_argument('-v', '--verify', action='store_true', help='verify flash contents after upload')
	parser.add_argument('--erase', type=str, help='erase the whole flash partition')
	parser.add_argument('-f', '--file', type=str, default='file.bin', help='name of the file to read from/write to')
	parser.add_argument('-b', '--block', type=int, default=DEFAULT_BLOCK_SIZE, help=f'transfer block size in bytes (default: {DEFAULT_BLOCK_SIZE})')

	return parser.parse_args()


class NbusClient:
	"""CBOR request/response over a pynbus2 socket."""

	def __init__(self, sock: pynbus2.NbusSocket):
		self._sock = sock

	def call(self, req: dict):
		reply = self._sock.request(cbor2.dumps(req), timeout=0.10, retries=20)
		if reply is None:
			return None
		try:
			return cbor2.loads(reply)
		except Exception:
			return None


class FlashClient:

	def __init__(self, n: NbusClient, block_size=DEFAULT_BLOCK_SIZE):
		self._n = n
		self._page = block_size

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
		page_size = self._page

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

	def _erase_all(self, flash_size, erase_size):
		with tqdm(desc='Erase', total=flash_size, ncols=120, unit='B', unit_scale=True, unit_divisor=1024) as progress_bar:
			for i in range(0, flash_size, erase_size):
				self._erase(i, erase_size)
				progress_bar.update(erase_size)

	def erase(self, vol):
		r = self.info(vol)
		flash_size = r.get('sizes')[0].get('s')
		erase_size = r.get('sizes')[1].get('s')

		self._open(vol)
		self._erase_all(flash_size, erase_size)
		self._close()

	def upload(self, vol, fname):
		r = self.info(vol)
		flash_size = r.get('sizes')[0].get('s')
		erase_size = r.get('sizes')[1].get('s')
		page_size = self._page

		self._open(vol)
		self._erase_all(flash_size, erase_size)

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

	def verify(self, vol, fname):
		with open(fname, 'rb') as f:
			original = f.read()

		page_size = self._page
		read_size = ((len(original) + page_size - 1) // page_size) * page_size

		self._open(vol)
		d = b''
		with tqdm(desc='Verify', total=read_size, ncols=120, unit='B', unit_scale=True, unit_divisor=1024) as progress_bar:
			for i in range(0, read_size, page_size):
				d += self._read(i, page_size)
				progress_bar.update(page_size)
		self._close()

		if d[:len(original)] == original:
			print(f'{Fore.GREEN}{Style.BRIGHT}Verify OK{Style.RESET_ALL}')
		else:
			for i, (a, b) in enumerate(zip(original, d[:len(original)])):
				if a != b:
					print(f'{Fore.RED}{Style.BRIGHT}Verify FAILED{Style.RESET_ALL}: first mismatch at offset 0x{i:08x} (expected 0x{a:02x}, got 0x{b:02x})')
					break
			sys.exit(1)



if __name__ == "__main__":

	colorama_init()
	args = init_args()

	with pynbus2.connect(args.uri) as nbus:
		sock = nbus.socket()
		if not sock._connected:
			print(f'the URI must carry a destination, e.g. udp6:///<sid>/<ep>', file=sys.stderr)
			sys.exit(1)
		n = NbusClient(sock)
		f = FlashClient(n, args.block)

		if args.list:
			f.list()
		if args.erase:
			f.erase(args.erase)
		if args.download:
			f.download(args.download, args.file)
		if args.upload:
			f.upload(args.upload, args.file)
			if args.verify:
				f.verify(args.upload, args.file)
		if args.reset:
			f.reset()
