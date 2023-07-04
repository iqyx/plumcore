#!/usr/bin/env python3

import logging
import serial
from colorama import init as colorama_init, Fore, Back, Style
import argparse
from textwrap import dedent
import re
from tqdm import tqdm


def fs_stats(s, fs):
	s.write(b'/ files print\n');

	for l in s.readlines():
		l = l.rstrip().decode('utf-8')
		m = re.match(f'.*({fs}) *(\w+) KB *(\w+) KB.*$', l)
		if m:
			return (m.groups()[1], m.groups()[2])

def get_block(s, fs):
	s.write(f'/ files {fs} name="fifo" format="hex" cat\n'.encode('utf-8'));
	data = b''

	with tqdm(total=1024) as progress_bar:
		while (True):
			l = s.readline().rstrip().decode('utf-8')
			if l:
				if re.match('^[0-9a-f]{128}$', l):
					data += bytes.fromhex(l)
					progress_bar.update()
			else:
				break
	return data

def remove_block(s, fs):
	s.write(f'/ files {fs} name="fifo" remove\n'.encode('utf-8'));
	s.readlines()


parser = argparse.ArgumentParser(
	description=dedent("""
		plumCore FIFO downloader sample application
	"""),
	epilog="(c) 2023 Marek Koza <qyx@krtko.org>",
	formatter_class=argparse.RawDescriptionHelpFormatter
)

parser.add_argument('--device', type=str, default='/dev/ttyUSB0', help='Serial port device')
parser.add_argument('--baud', type=int, default=115200, help='Serial port baud rate')
parser.add_argument('--debug', action='store_true', help='DEBUG level logging')
parser.add_argument('--fs', type=str, default='fifo', help='FIFO filesystem name (default "fifo")')
parser.add_argument('--out', type=str, default='fifo.bin', help='Output file name')

args = parser.parse_args()

colorama_init()
LOG_FORMAT = f"""[{Style.BRIGHT}{Fore.WHITE}%(asctime)s{Style.NORMAL}] {Fore.BLUE}{Style.BRIGHT}%(levelname)-10s\
{Fore.YELLOW}{Style.NORMAL}%(module)s:%(name)s: {Style.RESET_ALL}%(message)s"""

l = logging.INFO
if args.debug:
	l = logging.DEBUG

logging.basicConfig(format=LOG_FORMAT, level=l)

s = serial.Serial(args.device, args.baud, timeout=1)

(used_kb, size_kb) = fs_stats(s, args.fs)
logging.info(f'FIFO filesystem size {size_kb} KB')

block = 0
data_len = 0
with open(args.out, 'wb') as f:
	while (True):
		(used_kb, size_kb) = fs_stats(s, args.fs)
		logging.info(f'receiving block, current FIFO size {used_kb} KB')
		data = get_block(s, args.fs)
		logging.info(f'block received, length = {len(data)} B')

		if len(data) > 0:
			f.write(data)
			data_len += len(data)
		else:
			logging.info(f'end of FIFO reached, total download length {data_len // 1024} KB')
			break

		# advance to the next block
		logging.info(f'removing the last block')
		remove_block(s, args.fs)
		block += 1

s.close()
