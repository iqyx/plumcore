# SPDX-License-Identifier: GPL-3.0-or-later
#
# Client of the nbus-mq-poll service protocol
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""A client polling the message queue of a device through the firmware ``services/nbus-mq-poll`` bridge,
exported as an interface speaking the ``mq`` protocol.

Every poll request carries ``{"a": <cursor>, "m": <max_batches>}`` and optionally ``"t": <topic>``: the
cursor acknowledges the highest batch sequence number received contiguously so far, ``max_batches`` caps
the reply and the topic filter replaces the remote message queue subscription, so only matching values
are buffered and polled. The remote answers with ``{"h": <device>, "p": <pending>, "q": <buffering>,
"b": [<batch>, ...]}``, each batch a map ``{"d": [{"ts", "tn", "to", "v"}, ...], "s": <seq>}``. Batches
are deduplicated and ordered by sequence number, so a lost request or reply only causes a harmless
retransmission; ``pending`` drives back-to-back draining of a backlog.

    with pynbus2.connect('udp6:///00010002/3') as nbus:
        client = MqPollClient(nbus.socket(), topic='#')
        reply = client.poll()
"""

import logging
from dataclasses import dataclass, field
from typing import Any, Optional

import cbor2

logger = logging.getLogger(__name__)

#: Protocol name advertised by the interfaces exporting the firmware service.
PROTOCOL = 'mq'

#: Default number of batches requested per poll ("m" in the request), bounded further by the remote reply
#: buffer.
DEFAULT_MAX_BATCHES = 4


def serial_after(a, b):
	"""True when 32-bit sequence number ``a`` is strictly newer than ``b`` under serial-number arithmetic."""
	d = (a - b) & 0xffffffff
	return 0 < d < 0x80000000


def topic_matches(filter, topic):
	"""Whether ``topic`` matches the MQTT-style topic ``filter``: ``+`` matches a single level, a trailing
	``#`` the level before it and all the levels below."""
	filter_levels = filter.split('/')
	topic_levels = topic.split('/')
	for i, level in enumerate(filter_levels):
		if level == '#':
			return i == len(filter_levels) - 1
		if i >= len(topic_levels):
			return False
		if level != '+' and level != topic_levels[i]:
			return False
	return len(filter_levels) == len(topic_levels)


@dataclass(frozen=True)
class MqValue:
	"""One value received from the remote message queue."""
	#: The name of the device which published it, ``None`` if not known.
	host: Optional[str]
	#: The time the value was published at as seconds since the epoch on the device clock, ``None`` if
	#: not known.
	ts: Optional[float]
	topic: str
	#: The value as decoded from CBOR, e.g. a float. ``cbor2.undefined`` for a type the device does not
	#: serialise.
	value: Any


@dataclass(frozen=True)
class MqReply:
	"""The decoded reply to one poll request."""
	#: The values of the batches not received before, in order.
	values: list = field(default_factory=list)
	#: How many ready batches the remote still holds after this reply.
	pending: int = 0
	#: How many values sit in the remote batch not sealed yet.
	buffering: int = 0
	#: How many batches were missed (e.g. evicted by the remote) before the batches of this reply.
	missed: int = 0


class MqPollClient:
	"""Polls a remote ``mq`` interface over a connected :class:`~pynbus2.NbusSocket`.

	The client keeps the cursor of the batches received, it is not thread safe. The remote serves a single
	cursor, two clients polling the same interface would acknowledge each other's batches.
	"""

	def __init__(self, socket, max_batches=DEFAULT_MAX_BATCHES, topic=None):
		self._socket = socket
		self._max_batches = max_batches
		#: The topic filter sent with every request, ``None`` keeps the remote subscription.
		self.topic = topic
		# Highest batch sequence number seen contiguously; 0 means nothing received yet.
		self._cursor = 0

	def request(self):
		"""The encoded poll request acknowledging the batches received so far."""
		req = {'a': self._cursor, 'm': self._max_batches}
		if self.topic is not None:
			# Sent with every request so the subscription is restored after a remote restart.
			req['t'] = self.topic
		return cbor2.dumps(req)

	def decode(self, buf):
		"""Decode one received reply into a :class:`MqReply`, advancing the cursor past the new batches and
		dropping the batches received before. ``None`` if the reply cannot be decoded."""
		try:
			reply = cbor2.loads(buf)
		except Exception as e:
			logger.error('undecodable reply (%d bytes): %s', len(buf), e)
			return None
		if not isinstance(reply, dict):
			logger.error('unexpected reply type %s, expected a map', type(reply).__name__)
			return None

		host = reply.get('h')
		values = []
		missed = 0
		for batch in reply.get('b', []) or []:
			if not isinstance(batch, dict):
				continue
			seq = batch.get('s')
			if not isinstance(seq, int):
				continue

			if not serial_after(seq, self._cursor):
				# Already applied (a retransmission of an unacknowledged batch), skip it.
				continue
			if self._cursor != 0 and serial_after(seq, (self._cursor + 1) & 0xffffffff):
				gap = (seq - self._cursor - 1) & 0xffffffff
				logger.warning('sequence gap, missed %d batch(es) before seq %d', gap, seq)
				missed += gap

			for item in batch.get('d', []) or []:
				if not isinstance(item, dict):
					continue
				topic = item.get('to')
				if not isinstance(topic, str) or 'v' not in item:
					logger.warning('skipping malformed value %r', item)
					continue
				ts = item.get('ts')
				if ts is not None and item.get('tn'):
					ts = ts + item.get('tn') / 1e9
				values.append(MqValue(host, ts, topic, item['v']))

			self._cursor = seq
		return MqReply(values, reply.get('p') or 0, reply.get('q') or 0, missed)

	def poll(self, timeout=None):
		"""Send one poll request and return the :class:`MqReply` received.

		Returns ``None`` when no valid reply arrives within ``timeout`` (by default the request timeout of
		the socket's transport), so the caller can tell an empty reply (the remote has nothing pending)
		from a missing one. The request is idempotent, a lost one is simply sent again by the next poll.
		"""
		self._socket.send(self.request())
		buf = self._socket.recv(self._socket.request_timeout if timeout is None else timeout)
		if buf is None:
			logger.debug('no reply to poll request')
			return None
		return self.decode(buf)
