#!/usr/bin/env python3

import sys
import os
import time
import logging
from colorama import init as colorama_init, Fore, Back, Style
import argparse
import cbor2
import datetime

# Allow running straight from the source tree without installing pynbus2.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pynbus2', 'src'))
import pynbus2


# Interval between two poll requests in milliseconds when the remote has nothing pending. A backlog is
# drained as fast as it arrives (see MqPollClient.run), this only paces the idle polling.
DEFAULT_POLL_INTERVAL_MS = 1000

# Default number of batches requested per poll ("m" in the request). Bounded further by the remote
# reply buffer.
DEFAULT_MAX_BATCHES = 4


def serial_after(a, b):
	"""True when 32-bit sequence number a is strictly newer than b under serial-number arithmetic."""
	d = (a - b) & 0xffffffff
	return 0 < d < 0x80000000


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
		description="plumCore nbus-mq-poll client: poll a remote message queue and print the carried values",
		epilog="example: proto-mq-client.py udp6:///00010002/3\n\n(c) 2026 Marek Koza <qyx@krtko.org>",
		formatter_class=argparse.RawDescriptionHelpFormatter
	)

	parser.add_argument('--debug', action='store_true', help='DEBUG level logging')
	parser.add_argument('uri', type=str, help='nbus2 connection URI carrying the destination, e.g. udp6:///<sid>/<ep>')
	parser.add_argument('-i', '--interval', type=int, default=DEFAULT_POLL_INTERVAL_MS,
	                    help=f'idle poll interval in milliseconds (default: {DEFAULT_POLL_INTERVAL_MS})')
	parser.add_argument('-p', '--prefix', type=str, default='',
	                    help='prefix prepended as "<prefix>/<topic>" to every printed topic')
	parser.add_argument('-n', '--count', type=int, default=0,
	                    help='number of poll requests to send, then exit (default: 0 = poll forever)')
	parser.add_argument('-m', '--max-batches', type=int, default=DEFAULT_MAX_BATCHES,
	                    help=f'maximum batches requested per poll (default: {DEFAULT_MAX_BATCHES})')
	parser.add_argument('-t', '--topic', type=str, default=None,
	                    help='topic filter the remote subscribes to, e.g. "math/#" (default: keep the remote '
	                         'subscription)')

	return parser.parse_args()


log = logging.getLogger('proto-mq-client')


class MqPollClient:
	"""Poll a remote nbus-mq-poll service and print every value it returns.

	Every poll request carries { "a": <cursor>, "m": <max_batches> } and optionally "t": <topic>: the
	cursor acknowledges the highest batch sequence number received contiguously so far, max_batches caps
	the reply and the topic filter replaces the remote message queue subscription, so only matching
	values are buffered and polled. The remote answers with { "h": <device>, "p": <pending>,
	"q": <buffering>, "b": [ <batch>, ... ] }, each batch a map { "d": [ {"ts","tn","to","v"}, ... ],
	"s": <seq> }. Batches are deduplicated and ordered by sequence number, so a lost request or reply only
	causes a harmless retransmission; "p" drives back-to-back draining of a backlog.
	"""

	def __init__(self, sock: pynbus2.NbusSocket, prefix='', interval_ms=DEFAULT_POLL_INTERVAL_MS,
	             max_batches=DEFAULT_MAX_BATCHES, topic=None):
		self._sock = sock
		self._prefix = prefix
		self._topic = topic
		self._interval = interval_ms / 1000.0
		self._max_batches = max_batches
		# Highest batch sequence number seen contiguously; 0 means nothing received yet.
		self._cursor = 0

	def _decode_reply(self, buf):
		"""Decode one received reply into (values, pending).

		``values`` is a list of (host, ts, topic, value) tuples for the batches not seen before, with
		the cursor advanced past them and duplicates dropped. ``pending`` is how many ready batches the
		remote still holds after this reply.
		"""
		try:
			reply = cbor2.loads(buf)
		except Exception as e:
			log.error('undecodable reply (%d bytes): %s', len(buf), e)
			return [], 0
		if not isinstance(reply, dict):
			log.error('unexpected reply type %s, expected a map', type(reply).__name__)
			return [], 0

		host = reply.get('h')
		pending = reply.get('p') or 0
		values = []
		for batch in reply.get('b', []) or []:
			if not isinstance(batch, dict):
				continue
			seq = batch.get('s')
			if not isinstance(seq, int):
				continue

			if not serial_after(seq, self._cursor):
				# Already applied (a retransmission of an unacknowledged batch), skip it.
				continue
			if serial_after(seq, (self._cursor + 1) & 0xffffffff):
				log.warning('sequence gap, missed %d batch(es) before seq %d',
				            (seq - self._cursor - 1) & 0xffffffff, seq)

			for item in batch.get('d', []) or []:
				if not isinstance(item, dict):
					continue
				topic = item.get('to')
				value = item.get('v')
				if topic is None or value is None:
					log.warning('skipping malformed value %r', item)
					continue
				ts = item.get('ts')
				if ts is not None and item.get('tn'):
					ts = ts + item.get('tn') / 1e9
				values.append((host, ts, topic, value))

			self._cursor = seq
		return values, pending

	def _print_value(self, host, ts, topic, value):
		full = f'{self._prefix}/{topic}' if self._prefix else topic
		when = datetime.datetime.fromtimestamp(ts).isoformat() if ts else '-'
		host_str = f'{Fore.MAGENTA}{host}{Style.RESET_ALL} ' if host else ''
		print(f'[{Fore.WHITE}{Style.DIM}{when}{Style.RESET_ALL}] {host_str}'
		      f'{Fore.BLUE}{Style.BRIGHT}{full}{Style.RESET_ALL} = {value}')

	def poll(self):
		"""Send one poll request and return (values, pending) of the received reply.

		Returns ``None`` when no reply arrives (a lost request or response), so the caller can tell an
		empty reply (remote has nothing pending) from a missing one.
		"""
		req = {'a': self._cursor, 'm': self._max_batches}
		if self._topic is not None:
			# Sent with every request so the subscription is restored after a remote restart.
			req['t'] = self._topic
		self._sock.send(cbor2.dumps(req))
		reply = self._sock.recv(self._sock.request_timeout)
		if reply is None:
			log.debug('no reply to poll request')
			return None
		return self._decode_reply(reply)

	def run(self, count=0):
		"""Poll the remote in a loop, printing the received values.

		A reply reporting a remaining backlog is followed immediately by another poll so the backlog
		drains as fast as it arrives; an empty or missing reply is followed by an idle wait of the poll
		interval. Stops after ``count`` requests, or runs forever when ``count`` is 0.
		"""
		sent = 0
		while count == 0 or sent < count:
			result = self.poll()
			sent += 1

			if result is None:
				time.sleep(self._interval)
				continue
			values, pending = result

			for host, ts, topic, value in values:
				self._print_value(host, ts, topic, value)

			if pending == 0:
				time.sleep(self._interval)


if __name__ == "__main__":

	args = init_args()
	init_logging()

	with pynbus2.connect(args.uri) as nbus:
		sock = nbus.socket()
		if not sock._connected:
			print(f'the URI must carry a destination, e.g. udp6:///<sid>/<ep>', file=sys.stderr)
			sys.exit(1)

		client = MqPollClient(sock, args.prefix, args.interval, args.max_batches, args.topic)
		try:
			client.run(args.count)
		except KeyboardInterrupt:
			log.info('interrupted, exiting')
