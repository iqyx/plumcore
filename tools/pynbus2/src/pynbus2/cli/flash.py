# SPDX-License-Identifier: GPL-3.0-or-later
#
# Command line client of the nbus-flash service
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""``pynbus2-flash``: list, read, erase and write the flash partitions of a device exported by nbus-flash."""

import argparse
import logging
import sys

from colorama import Fore, Style
from tqdm import tqdm

from . import add_common_args, init_logging, connect_socket, fail
from ..flash import FlashClient, FlashError

logger = logging.getLogger('pynbus2-flash')


def progress_bar(desc, total):
	return tqdm(desc=desc, total=total, ncols=120, unit='B', unit_scale=True, unit_divisor=1024)


def progress(bar):
	"""A progress callback updating ``bar`` to the number of bytes done so far."""
	def update(done):
		bar.update(done - bar.n)
	return update


def list_partitions(client):
	for name in client.list():
		size, erase_size, page_size = client.info(name)
		s = f'volume = {size // 1024} KB'
		if erase_size:
			s += f', erase block: {erase_size // 1024} KB'
		if page_size:
			s += f', page: {page_size} B'
		print(f'Volume {Fore.BLUE}{Style.BRIGHT}{name}{Style.RESET_ALL}: {s}')


def download(client, name, fname):
	size, _, _ = client.info(name)
	with open(fname, 'wb') as f, progress_bar('Download', size) as bar:
		client.read(name, size, f, progress=progress(bar))


def erase(client, name):
	size, erase_size, _ = client.info(name)
	if erase_size is None:
		fail(f'partition {name!r} cannot be erased')
	with progress_bar('Erase', size) as bar:
		client.erase(name, size, erase_size, progress=progress(bar))


def upload(client, name, fname):
	size, erase_size, page_size = client.info(name)
	if erase_size is None or page_size is None:
		fail(f'partition {name!r} cannot be written')
	with open(fname, 'rb') as f:
		data = f.read()

	# The library reports the stages (erase, write), each with its own progress bar.
	bar = None

	def stage(what, total):
		nonlocal bar
		if bar is not None:
			bar.close()
		bar = progress_bar(what.capitalize(), total)

	try:
		client.write(name, size, erase_size, page_size, data, progress=lambda done: bar.update(done - bar.n),
		             stage=stage, verify=False)
	finally:
		if bar is not None:
			bar.close()


def diagnose_block(addr, expected, got, blocks):
	"""Classify why a read-back block differs from what was uploaded, for a readable failure line.

	``blocks`` maps a block address to the bytes that were uploaded there, so a mismatching block can be tested
	against its neighbours: a block that carries another block's contents is a desynced or duplicated read, not
	corrupted flash."""
	if got is None:
		return 'read failed'
	if len(got) != len(expected):
		return f'short/long read ({len(got)} of {len(expected)} bytes)'
	# A whole-block swap points at a reply/response desync rather than a bad flash cell.
	for other_addr, other in blocks.items():
		if other_addr != addr and got == other:
			delta = (other_addr - addr) // len(expected)
			return f'holds block {other_addr:#010x} instead ({delta:+d} blocks away): desynced read'
	if got == b'\xff' * len(got):
		return 'all 0xff: block reads as erased (write lost, or read hit the wrong address)'
	diffs = sum(1 for a, b in zip(expected, got) if a != b)
	first = next(i for i, (a, b) in enumerate(zip(expected, got)) if a != b)
	return (f'{diffs}/{len(got)} bytes differ, first at +0x{first:02x} '
	        f'(expected 0x{expected[first]:02x}, got 0x{got[first]:02x})')


def verify(client, name, fname):
	"""Read back the uploaded file and compare it block by block, logging a diagnosis of every mismatching
	block. Exits if any differ."""
	with open(fname, 'rb') as f:
		original = f.read()

	block = client.TRANSFER_BLOCK
	read_size = -(-len(original) // block) * block

	# The uploaded image is the file padded to a whole number of blocks with 0xff. Comparing whole padded blocks
	# keeps read and expectation aligned and lets a mismatching block be matched against its neighbours to tell
	# a desynced read from real corruption.
	image = original + b'\xff' * (read_size - len(original))
	blocks = {addr: image[addr:addr + block] for addr in range(0, read_size, block)}

	mismatches = []
	with client.session(name) as sid, progress_bar('Verify', read_size) as bar:
		for addr in range(0, read_size, block):
			got = client._read_block(sid, addr, block, check=False)
			if got != blocks[addr]:
				logger.error('verify mismatch at 0x%08x: %s', addr, diagnose_block(addr, blocks[addr], got, blocks))
				mismatches.append(addr)
			bar.update(block)

	if not mismatches:
		print(f'{Fore.GREEN}{Style.BRIGHT}Verify OK{Style.RESET_ALL}')
	else:
		print(f'{Fore.RED}{Style.BRIGHT}Verify FAILED{Style.RESET_ALL}: '
		      f'{len(mismatches)}/{read_size // block} block(s) differ, first at offset 0x{mismatches[0]:08x} '
		      f'(see the logged per-block diagnostics above)')
		sys.exit(1)


def main(argv=None):
	parser = argparse.ArgumentParser(
		description='List, read, erase and write the flash partitions of a device exported by nbus-flash.',
		epilog='example: pynbus2-flash udp6:///00010002/1 --list\n'
		       '         pynbus2-flash rible://nwdaq-hh1/flash -u calib -f calib.bin',
		formatter_class=argparse.RawDescriptionHelpFormatter,
	)
	add_common_args(parser)
	parser.add_argument('-l', '--list', action='store_true', help='list flash partitions')
	parser.add_argument('-r', '--reset', action='store_true', help='reset the device')
	parser.add_argument('-d', '--download', type=str, metavar='PARTITION',
	                    help='download content of the flash partition')
	parser.add_argument('-u', '--upload', type=str, metavar='PARTITION',
	                    help='upload content to the flash volume')
	parser.add_argument('-v', '--verify', action='store_true', help='verify flash contents after upload')
	parser.add_argument('--erase', type=str, metavar='PARTITION', help='erase the whole flash partition')
	parser.add_argument('-f', '--file', type=str, default='file.bin',
	                    help='name of the file to read from/write to (default: %(default)s)')
	parser.add_argument('-b', '--block', type=int, default=FlashClient.TRANSFER_BLOCK,
	                    help='transfer block size in bytes (default: %(default)s)')
	args = parser.parse_args(argv)
	init_logging(args.debug)

	nbus, sock = connect_socket(args.uri)
	with nbus:
		client = FlashClient(sock)
		client.TRANSFER_BLOCK = args.block
		try:
			if args.list:
				list_partitions(client)
			if args.erase:
				erase(client, args.erase)
			if args.download:
				download(client, args.download, args.file)
			if args.upload:
				upload(client, args.upload, args.file)
				if args.verify:
					verify(client, args.upload, args.file)
			if args.reset:
				# The reply is not required, the device may reset before sending it.
				try:
					client.call({'c': 'reset'})
				except FlashError:
					pass
		except FlashError as e:
			fail(e)
	return 0


if __name__ == '__main__':
	sys.exit(main())
