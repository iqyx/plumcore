#!/usr/bin/env python3

import sys
import os
import select
import logging
from colorama import init as colorama_init, Fore, Back, Style
import argparse
import datetime

# Allow running straight from the source tree without installing pynbus2.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pynbus2', 'src'))
import pynbus2

from proto_stream import ProtoStream


# Escape byte that quits the bridge when stdin is a raw terminal (otherwise Ctrl-C, which is passed
# through verbatim in raw mode, could never reach us). Ctrl-] mirrors telnet/picocom conventions.
QUIT_BYTE = 0x1d

# How long to wait for stdin activity between device polls, in seconds.
DEFAULT_POLL = 0.02


LOG_FORMAT = f"""[{Style.BRIGHT}{Fore.WHITE}%(asctime)s{Style.NORMAL}] {Fore.BLUE}{Style.BRIGHT}%(levelname)-10s{Fore.YELLOW}{Style.NORMAL}%(module)s:%(name)s: {Style.RESET_ALL}%(message)s"""


class CustomLogFormatter(logging.Formatter):

	color_codes = {
		'DEBUG': Style.BRIGHT + Fore.GREEN,
		'INFO': Style.BRIGHT + Fore.BLUE,
		'WARNING': Style.BRIGHT + Fore.YELLOW,
		'ERROR': Style.BRIGHT + Fore.RED,
		'CRITICAL': Style.BRIGHT + Fore.MAGENTA,
	}

	def format(self, record):
		levelname = CustomLogFormatter.color_codes.get(record.levelname, '') + f'{record.levelname:10}'
		asctime = datetime.datetime.fromtimestamp(record.created).isoformat()
		return f'[{Style.DIM}{Fore.WHITE}{asctime}{Style.NORMAL}] {levelname}{Style.RESET_ALL} {Fore.YELLOW}{Style.NORMAL}{record.module}:{record.name}:{Style.RESET_ALL} {record.getMessage()}'


def init_logging():
	colorama_init()

	l = logging.INFO
	if args.debug:
		l = logging.DEBUG

	logging.basicConfig(level=l)
	logging.getLogger().handlers[0].setFormatter(CustomLogFormatter())


def init_args():
	parser = argparse.ArgumentParser(
		description="plumCore proto-stream bridge: connect a device stream to stdin/stdout over nbus2",
		epilog="example: proto-stream-stdio.py udp6:///00010002/4\n\n(c) 2026 Marek Koza <qyx@krtko.org>",
		formatter_class=argparse.RawDescriptionHelpFormatter
	)

	parser.add_argument('--debug', action='store_true', help='DEBUG level logging')
	parser.add_argument('uri', type=str, help='nbus2 connection URI carrying the destination, e.g. udp6:///<sid>/<ep>')
	parser.add_argument('--no-raw', action='store_true', help='do not put an interactive terminal into raw mode')
	parser.add_argument('-p', '--poll', type=float, default=DEFAULT_POLL, help=f'stdin/device poll interval in seconds (default: {DEFAULT_POLL})')

	return parser.parse_args()


log = logging.getLogger('proto-stream-stdio')


class RawTerminal:
	"""Put a terminal into raw mode for the duration of the ``with`` block, restoring it afterwards.

	A no-op when the file descriptor is not a tty (e.g. stdin is a pipe or a file), so the bridge works
	unchanged in a non-interactive pipeline.
	"""

	def __init__(self, fd, enable=True):
		self._fd = fd
		self._enable = enable
		self._saved = None

	def __enter__(self):
		if self._enable and os.isatty(self._fd):
			import termios
			import tty
			self._saved = termios.tcgetattr(self._fd)
			tty.setraw(self._fd)
		return self

	@property
	def is_raw(self):
		return self._saved is not None

	def __exit__(self, *exc):
		if self._saved is not None:
			import termios
			termios.tcsetattr(self._fd, termios.TCSADRAIN, self._saved)


def bridge(stream, poll, raw):
	"""Pump bytes both ways between ``stream`` and stdin/stdout until EOF or the quit key."""
	stdin_fd = sys.stdin.fileno()
	stdout = sys.stdout.buffer
	stdin_open = True

	while True:
		# Device -> stdout: drain whatever the device has buffered right now (returns promptly).
		data = stream.read(-1)
		if data:
			stdout.write(data)
			stdout.flush()

		# Stdin -> device: wait briefly for input so we do not busy-loop between device polls.
		ready, _, _ = select.select([stdin_fd] if stdin_open else [], [], [], poll)
		if not ready:
			continue

		chunk = os.read(stdin_fd, ProtoStream.BLOCK_LEN)
		if not chunk:
			# End of input. In raw interactive mode that means we are done; on a pipe keep draining the
			# device output (the caller quits with Ctrl-C) since replies may still be arriving.
			stdin_open = False
			if raw:
				return
			continue

		if raw and QUIT_BYTE in chunk:
			chunk = chunk[:chunk.index(QUIT_BYTE)]
			if chunk:
				stream.write(chunk)
			return

		stream.write(chunk)


if __name__ == "__main__":

	args = init_args()
	init_logging()

	with pynbus2.connect(args.uri) as nbus:
		sock = nbus.socket()
		if not sock._connected:
			print('the URI must carry a destination, e.g. udp6:///<sid>/<ep>', file=sys.stderr)
			sys.exit(1)

		stream = ProtoStream(sock, poll_interval=args.poll)

		with RawTerminal(sys.stdin.fileno(), enable=not args.no_raw) as term:
			raw = term.is_raw
			if raw:
				log.info('connected, raw terminal mode; press Ctrl-] to quit')
			else:
				log.info('connected; press Ctrl-C to quit')
			try:
				bridge(stream, args.poll, raw)
			except KeyboardInterrupt:
				pass
			except TimeoutError as e:
				log.error('%s', e)
				sys.exit(1)
