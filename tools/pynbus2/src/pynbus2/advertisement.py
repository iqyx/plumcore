# SPDX-License-Identifier: GPL-3.0-or-later
#
# nbus2 descriptor advertisement parsing
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Descriptor advertisements: the periodic multicast packets nbus2 services emit to announce themselves.

Every couple of seconds each plumCore service walks its active sockets and advertises the descriptor of
every socket that has one (see services/nbus2/nbus2.c, ``nbus_socket_advertise``): a multicast packet
sent from the socket's own ID/endpoint to the well-known descriptor multicast address ``00000101``
endpoint 0, carrying a CBOR map that names the protocol spoken on the socket. A host speaking the nbus2
wire protocol itself (the serial transports) receives these on its reader thread and can build a live
picture of the services out there.

The CBOR map keys (see nbus2.rst, "Descriptor advertisement"):

    adv  unsigned   advertisement protocol version (currently 1)
    p    text       protocol name spoken on the socket
    pv   text       protocol version (optional; omitted when not set)

Keys that are not set are omitted from the map. Decoding needs ``cbor2``.
"""

import collections
import logging

from .addressing import parse_id, format_id

#: Well-known multicast destination descriptor advertisements are sent to (NBUS_ADV_MULTICAST_ID/EP).
ADV_MULTICAST_ID = parse_id('00000101')
ADV_MULTICAST_EP = 0

#: Advertisement protocol version this module understands (NBUS_ADV_VERSION).
ADV_VERSION = 1

logger = logging.getLogger(__name__)

#: A parsed descriptor advertisement: who sent it, the protocol version and the descriptor keys.
Advertisement = collections.namedtuple(
	'Advertisement', 'src_id src_ep version protocol protocol_version keys')


def is_advertisement(pkt):
	"""Whether ``pkt`` is a descriptor advertisement: a multicast packet to the well-known address."""
	return pkt.multicast and pkt.dst_id == ADV_MULTICAST_ID and pkt.dst_ep == ADV_MULTICAST_EP


def parse_advertisement(pkt):
	"""Deserialize the CBOR advertisement payload of ``pkt`` into an :class:`Advertisement`.

	Returns ``None`` if the payload is not a well-formed advertisement (bad CBOR or not a map).
	"""
	import cbor2

	try:
		keys = cbor2.loads(pkt.payload)
	except Exception:
		return None
	if not isinstance(keys, dict):
		return None
	return Advertisement(
		src_id=pkt.src_id,
		src_ep=pkt.src_ep,
		version=keys.get('adv'),
		protocol=keys.get('p'),
		protocol_version=keys.get('pv'),
		keys=keys,
	)


def log_advertisement(pkt):
	"""Deserialize a received descriptor advertisement and log it at DEBUG (the default handler).

	``pkt`` must already be an advertisement (see :func:`is_advertisement`).
	"""
	if not logger.isEnabledFor(logging.DEBUG):
		return
	try:
		adv = parse_advertisement(pkt)
	except ImportError:
		logger.debug('descriptor advertisement from %s.%d (install cbor2 to decode it)',
		             format_id(pkt.src_id), pkt.src_ep)
		return
	if adv is None:
		logger.debug('malformed descriptor advertisement from %s.%d: %r',
		             format_id(pkt.src_id), pkt.src_ep, pkt.payload)
	else:
		logger.debug('descriptor advertisement from %s.%d: adv=%s p=%r pv=%r',
		             format_id(adv.src_id), adv.src_ep, adv.version, adv.protocol, adv.protocol_version)
