# SPDX-License-Identifier: GPL-3.0-or-later
#
# Command line client of the nbus-mq-poll service
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""``pynbus2-mq``: poll the message queue of a device through the nbus-mq-poll bridge and print the values."""

import argparse
import datetime
import json
import logging
import sys
import time

from colorama import Fore, Style

from . import add_common_args, init_logging, connect_socket
from ..mq import MqPollClient, DEFAULT_MAX_BATCHES

logger = logging.getLogger('pynbus2-mq')

#: Interval between two poll requests in milliseconds when the remote has nothing pending. A backlog is
#: drained as fast as it arrives, this only paces the idle polling.
DEFAULT_POLL_INTERVAL_MS = 1000

#: Delay before another connection attempt with --auto-reconnect, in seconds.
RECONNECT_DELAY = 1.0


def print_value(v, prefix):
	topic = f'{prefix}/{v.topic}' if prefix else v.topic
	when = datetime.datetime.fromtimestamp(v.ts).isoformat() if v.ts else '-'
	host = f'{Fore.MAGENTA}{v.host}{Style.RESET_ALL} ' if v.host else ''
	print(f'[{Fore.WHITE}{Style.DIM}{when}{Style.RESET_ALL}] {host}'
	      f'{Fore.BLUE}{Style.BRIGHT}{topic}{Style.RESET_ALL} = {v.value}', flush=True)


def json_default(o):
	"""Encode the CBOR values JSON cannot carry: a byte string as hex, undefined (a type the device does not
	serialise) as null."""
	if isinstance(o, bytes):
		return o.hex()
	return None


def print_value_json(v, prefix):
	"""Print the value as a single line JSON object (JSON Lines)."""
	topic = f'{prefix}/{v.topic}' if prefix else v.topic
	print(json.dumps({'host': v.host, 'ts': v.ts, 'topic': topic, 'value': v.value}, default=json_default),
	      flush=True)


def poll(client, interval, prefix, count, sent, print_value):
	"""Poll in a loop, printing the received values with ``print_value``. A reply reporting a backlog is followed
	immediately by another poll, an empty or missing one by an idle wait of ``interval`` seconds. Returns once
	``sent`` (the requests sent so far) reaches ``count``, or runs forever when ``count`` is 0."""
	while count == 0 or sent < count:
		reply = client.poll()
		sent += 1
		if reply is not None:
			for v in reply.values:
				print_value(v, prefix)
		if reply is None or reply.pending == 0:
			time.sleep(interval)
	return sent


def close(nbus):
	"""Close the connection, which may be lost already."""
	try:
		nbus.close()
	except Exception as e:
		logger.debug('closing the connection failed: %s', e)


def run(args, print_value):
	"""Connect and poll until ``args.count`` requests are sent. A lost connection (e.g. the device was reset) is
	reported and the tool exits, or it is connected again with ``args.auto_reconnect``. Returns the exit code."""
	sent = 0
	while True:
		try:
			nbus, sock = connect_socket(args.uri)
		except Exception as e:
			if not args.auto_reconnect:
				logger.error('cannot connect to the device: %s', e)
				return 1
			logger.warning('cannot connect to the device: %s, retrying', e)
			time.sleep(RECONNECT_DELAY)
			continue

		try:
			# A new client after each connect: the batch sequence numbers start over on a reset device.
			client = MqPollClient(sock, args.max_batches, args.topic)
			sent = poll(client, args.interval / 1000.0, args.prefix, args.count, sent, print_value)
			return 0
		except Exception as e:
			if not args.auto_reconnect:
				logger.error('disconnected from the device: %s', e)
				return 1
			logger.warning('disconnected from the device: %s, reconnecting', e)
		finally:
			close(nbus)


def main(argv=None):
	parser = argparse.ArgumentParser(
		description='Poll the message queue of a device through the nbus-mq-poll bridge and print the values.',
		epilog='example: pynbus2-mq udp6:///00010002/3\n'
		       '         pynbus2-mq rible://nwdaq-hh1/mq -t "sensor/#" -a',
		formatter_class=argparse.RawDescriptionHelpFormatter,
	)
	add_common_args(parser)
	parser.add_argument('-i', '--interval', type=int, default=DEFAULT_POLL_INTERVAL_MS,
	                    help='idle poll interval in milliseconds (default: %(default)s)')
	parser.add_argument('-p', '--prefix', type=str, default='',
	                    help='prefix prepended as "<prefix>/<topic>" to every printed topic')
	parser.add_argument('-n', '--count', type=int, default=0,
	                    help='number of poll requests to send, then exit (default: 0 = poll forever)')
	parser.add_argument('-m', '--max-batches', type=int, default=DEFAULT_MAX_BATCHES,
	                    help='maximum batches requested per poll (default: %(default)s)')
	parser.add_argument('-t', '--topic', type=str, default=None,
	                    help='topic filter the remote subscribes to, e.g. "math/#" (default: keep the remote '
	                         'subscription)')
	parser.add_argument('-j', '--json', action='store_true',
	                    help='print each value as a JSON object on its own line instead, with no logging at all. A '
	                         'lost connection is reported by a non-zero exit code only')
	parser.add_argument('-a', '--auto-reconnect', action='store_true',
	                    help='connect again when the connection is lost (e.g. the device was reset) or cannot be '
	                         'made, instead of exiting with an error')
	args = parser.parse_args(argv)
	if args.json:
		logging.disable(logging.CRITICAL)
	else:
		init_logging(args.debug)

	try:
		return run(args, print_value_json if args.json else print_value)
	except KeyboardInterrupt:
		logger.info('interrupted, exiting')
	return 0


if __name__ == '__main__':
	sys.exit(main())
