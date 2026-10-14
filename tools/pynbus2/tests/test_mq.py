# SPDX-License-Identifier: GPL-3.0-or-later
#
# Message queue poll client tests
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import cbor2
import pytest

from pynbus2.mq import MqPollClient, serial_after, topic_matches


class FakeSocket:
	"""Records the sent requests and answers each with the next queued reply, ``None`` for a lost one."""

	request_timeout = 0.1

	def __init__(self, replies):
		self.sent = []
		self._replies = list(replies)

	def send(self, data):
		self.sent.append(cbor2.loads(data))

	def recv(self, timeout):
		reply = self._replies.pop(0)
		return None if reply is None else cbor2.dumps(reply)


def batch(seq, *values):
	return {'d': [{'ts': 100, 'tn': 500000000, 'to': topic, 'v': v} for topic, v in values], 's': seq}


def test_poll_decodes_values_and_acknowledges():
	sock = FakeSocket([
		{'h': 'dev', 'p': 1, 'q': 2, 'b': [batch(7, ('a/b', 1.5), ('a/c', 2.0))]},
		{'h': 'dev', 'p': 0, 'q': 0, 'b': []},
	])
	client = MqPollClient(sock, max_batches=3, topic='a/#')

	reply = client.poll()
	assert [(v.host, v.ts, v.topic, v.value) for v in reply.values] == [
		('dev', 100.5, 'a/b', 1.5), ('dev', 100.5, 'a/c', 2.0)]
	assert (reply.pending, reply.buffering, reply.missed) == (1, 2, 0)

	client.poll()
	assert sock.sent == [{'a': 0, 'm': 3, 't': 'a/#'}, {'a': 7, 'm': 3, 't': 'a/#'}]


def test_retransmitted_batches_are_dropped():
	sock = FakeSocket([
		{'b': [batch(1, ('x', 1))]},
		{'b': [batch(1, ('x', 1)), batch(2, ('x', 2))]},
	])
	client = MqPollClient(sock)
	assert [v.value for v in client.poll().values] == [1]
	assert [v.value for v in client.poll().values] == [2]
	assert 't' not in sock.sent[0]


def test_gap_is_counted_after_the_first_batch():
	# The first batch received may have any sequence number, the remote ran before.
	sock = FakeSocket([{'b': [batch(40, ('x', 1))]}, {'b': [batch(43, ('x', 2))]}])
	client = MqPollClient(sock)
	assert client.poll().missed == 0
	assert client.poll().missed == 2


def test_missing_and_malformed_replies():
	sock = FakeSocket([None])
	assert MqPollClient(sock).poll() is None
	client = MqPollClient(FakeSocket([]))
	assert client.decode(b'\xff\x00') is None
	assert client.decode(cbor2.dumps([1, 2])) is None
	reply = client.decode(cbor2.dumps({'b': [{'d': [{'to': 'x'}, {'v': 1}, 'junk'], 's': 1}]}))
	assert reply.values == []


def test_serial_after_wraps():
	assert serial_after(1, 0xffffffff)
	assert not serial_after(0xffffffff, 1)
	assert not serial_after(5, 5)


@pytest.mark.parametrize('filter, topic, expected', [
	('#', 'a', True),
	('#', 'a/b/c', True),
	('a/#', 'a', True),
	('a/#', 'a/b/c', True),
	('a/#', 'b/c', False),
	('a/+', 'a/b', True),
	('a/+', 'a/b/c', False),
	('a/+/c', 'a/b/c', True),
	('a/b', 'a/b', True),
	('a/b', 'a/bc', False),
	('a/b', 'a', False),
])
def test_topic_matches(filter, topic, expected):
	assert topic_matches(filter, topic) == expected
