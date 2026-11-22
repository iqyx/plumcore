# SPDX-License-Identifier: GPL-3.0-or-later
#
# Command line tools of the pynbus2 service protocol clients
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Command line tools built on the protocol clients, installed as the ``pynbus2-*`` scripts (requires the
``cli`` extra). This module holds the parts they share."""

import datetime
import logging
import sys

from colorama import init as colorama_init, Fore, Style

from .. import connect


class LogFormatter(logging.Formatter):

	color_codes = {
		'DEBUG': Style.BRIGHT + Fore.GREEN,
		'INFO': Style.BRIGHT + Fore.BLUE,
		'WARNING': Style.BRIGHT + Fore.YELLOW,
		'ERROR': Style.BRIGHT + Fore.RED,
		'CRITICAL': Style.BRIGHT + Fore.MAGENTA,
	}

	def format(self, record):
		levelname = self.color_codes.get(record.levelname, '') + f'{record.levelname:10}'
		asctime = datetime.datetime.fromtimestamp(record.created).isoformat()
		return (f'[{Style.DIM}{Fore.WHITE}{asctime}{Style.NORMAL}] {levelname}{Style.RESET_ALL} '
		        f'{Fore.YELLOW}{Style.NORMAL}{record.name}:{Style.RESET_ALL} {record.getMessage()}')


def add_common_args(parser):
	"""Add the arguments every tool takes: the URI and the logging level."""
	parser.add_argument('uri', type=str,
	                    help='nbus2 connection URI carrying the destination, e.g. udp6:///<sid>/<ep> or '
	                         'rible://<device>/<interface>')
	parser.add_argument('--debug', action='store_true', help='DEBUG level logging')


def init_logging(debug=False):
	colorama_init()
	logging.basicConfig(level=logging.DEBUG if debug else logging.INFO)
	logging.getLogger().handlers[0].setFormatter(LogFormatter())


def connect_socket(uri):
	"""Connect to ``uri`` and return ``(nbus, socket)``, the socket connected to the destination the URI
	carries. Exits if it carries none."""
	nbus = connect(uri)
	sock = nbus.socket()
	if not sock._connected:
		nbus.close()
		print('the URI must carry a destination, e.g. udp6:///<sid>/<ep> or rible://<device>/<interface>',
		      file=sys.stderr)
		sys.exit(1)
	return nbus, sock


def fail(msg):
	"""Print ``msg`` as an error and exit."""
	print(f'{Fore.RED}{Style.BRIGHT}error:{Style.RESET_ALL} {msg}', file=sys.stderr)
	sys.exit(1)
