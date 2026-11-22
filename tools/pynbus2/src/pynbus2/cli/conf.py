# SPDX-License-Identifier: GPL-3.0-or-later
#
# Command line client of the proto-conf service
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""``pynbus2-conf``: walk, read and write the configuration tree of a device exported by proto-conf."""

import argparse
import logging
import sys

from colorama import Fore, Style

from . import add_common_args, init_logging, connect_socket, fail
from .. import connect
from ..conf import ConfClient, ConfError, parse_value, TYPE_NAMES, CONF_NONE, CONF_SUBTREE


def split_path(s):
	return [p for p in s.split('/') if p]


def fmt_value(val):
	if isinstance(val, bytes):
		return val.hex()
	return str(val)


def print_tree(client, path, prefix):
	children = client.children(path)
	for i, ch in enumerate(children):
		name = ch.get('n', '')
		type = ch.get('t', CONF_NONE)
		last = (i == len(children) - 1)
		connector = '└── ' if last else '├── '

		child_path = path + [name]
		line = (f'{prefix}{connector}{Fore.BLUE}{Style.BRIGHT}{name}{Style.RESET_ALL} '
		        f'{Style.DIM}({TYPE_NAMES.get(type, str(type))}){Style.RESET_ALL}')
		if type not in (CONF_NONE, CONF_SUBTREE):
			try:
				line += f' = {Fore.GREEN}{fmt_value(client.read(child_path))}{Style.RESET_ALL}'
			except ConfError:
				pass
		print(line)

		if type == CONF_SUBTREE:
			print_tree(client, child_path, prefix + ('    ' if last else '│   '))


def walk(client, path):
	print(f'{Fore.BLUE}{Style.BRIGHT}{"/".join(path) if path else "root"}{Style.RESET_ALL}')
	print_tree(client, path, '')


def write(client, path, value_str):
	type = client.stat(path).get('t', CONF_NONE)
	try:
		value = parse_value(type, value_str)
	except ValueError as e:
		fail(f'cannot parse {value_str!r} as {TYPE_NAMES.get(type, str(type))}: {e}')
	client.write(path, value, type)
	print('ok')


def list_interfaces(uri):
	with connect(uri) as nbus:
		for name, desc in nbus.walk():
			pv = desc.get('pv')
			print(f'{Fore.BLUE}{Style.BRIGHT}{name!r}{Style.RESET_ALL} {desc.get("p")}' + (f' {pv}' if pv else '') +
			      f' {Style.DIM}(mtu {desc.get("mtu")}, max sessions {desc.get("ms")}){Style.RESET_ALL}')


def main(argv=None):
	parser = argparse.ArgumentParser(
		description='Walk, read and write the configuration tree of a device exported by proto-conf.',
		epilog='example: pynbus2-conf udp6:///00010002/3 --walk\n'
		       '         pynbus2-conf rible://nwdaq-hh1/conf --walk\n'
		       '         pynbus2-conf rible://nwdaq-hh1 --list',
		formatter_class=argparse.RawDescriptionHelpFormatter,
	)
	add_common_args(parser)
	parser.add_argument('-L', '--list', action='store_true',
	                    help='list the interfaces exported by the device (rible:// only)')
	parser.add_argument('-w', '--walk', type=str, nargs='?', const='', default=None, metavar='PATH',
	                    help='walk and show the configuration tree at PATH, slash separated (default: root)')
	parser.add_argument('-r', '--read', type=str, default=None, metavar='PATH',
	                    help='read the value of the conf node at PATH and print it (script-friendly)')
	parser.add_argument('-W', '--write', type=str, nargs=2, default=None, metavar=('PATH', 'VALUE'),
	                    help='write VALUE to the conf node at PATH (slash separated)')
	parser.add_argument('-l', '--load', action='store_true',
	                    help='load the stored configuration on the device and wait for it to finish')
	parser.add_argument('-s', '--save', action='store_true',
	                    help='save the configuration on the device and wait for it to finish')
	parser.add_argument('-v', '--verbose', action='store_true', help='log all protocol requests and responses')
	args = parser.parse_args(argv)
	init_logging(args.debug)
	if args.verbose:
		logging.getLogger('pynbus2.conf').setLevel(logging.DEBUG)

	if args.list:
		list_interfaces(args.uri)
		return 0

	nbus, sock = connect_socket(args.uri)
	with nbus:
		client = ConfClient(sock)
		try:
			if args.walk is not None:
				walk(client, split_path(args.walk))
			if args.read is not None:
				print(fmt_value(client.read(split_path(args.read))))
			if args.write is not None:
				write(client, split_path(args.write[0]), args.write[1])
			if args.load:
				client.load()
				print('ok')
			if args.save:
				client.save()
				print('ok')
		except ConfError as e:
			fail(e)
	return 0


if __name__ == '__main__':
	sys.exit(main())
