# SPDX-License-Identifier: GPL-3.0-or-later
#
# BLAKE2s-only SIV-mode packet protection for nbus2
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Single-primitive SIV-mode authenticated encryption built entirely on BLAKE2s.

BLAKE2s (RFC 7693) is used as the *only* cryptographic primitive: keyed, it serves as the MAC that
produces the synthetic IV (SIV), and, in a counter mode, it produces the stream-cipher keystream.
Nothing here is hand-rolled crypto - it only composes :func:`hashlib.blake2s`, which is the standard
library's vetted BLAKE2s. This mirrors the firmware's ``services/nbus2/blake2s-siv.c`` byte for byte,
so a packet built here authenticates and decrypts on the device and vice versa.

The construction (a non-AES instance of the generic SIV / S2V mode, cf. draft-madden-generalised-siv):

    (ke ‖ km) = BLAKE2s(preshared_key)                 # 32-byte digest split into two 16-byte keys
    SIV       = BLAKE2s(key=km, header ‖ plaintext)[:8]  # 64-bit tag, doubles as the IV
    keystream = BLAKE2s(key=ke, SIV ‖ counter_be32) ‖ …  # 32 bytes per block, counter from 0
    ciphertext = (header ‖ plaintext) XOR keystream

The 64-bit SIV is deliberately short to keep per-packet overhead low. It is adequate for this
online/realtime obstruction layer: a forgery costs one live interaction per 2**-64 success
probability and there is no offline grinding, because the keystream itself depends on the tag.
"""

import hashlib

#: ChaCha-free: lengths of the derived encryption and MAC keys, in bytes.
KE_LEN = 16
KM_LEN = 16

#: SIV / MAC tag length in bytes (64 bits).
TAG_LEN = 8

#: BLAKE2s digest size used internally for the MAC and each keystream block.
_DIGEST = 32


def derive_keys(preshared_key):
	"""Derive the encryption key ``ke`` and the MAC key ``km`` from the preshared key.

	A single keyless BLAKE2s of the preshared key yields 32 bytes which are split into ``ke`` (first
	16) and ``km`` (last 16). Matches ``b2s_derive_keys()``.
	"""
	if isinstance(preshared_key, str):
		preshared_key = preshared_key.encode()
	digest = hashlib.blake2s(preshared_key, digest_size=_DIGEST).digest()
	return digest[:KE_LEN], digest[KE_LEN:KE_LEN + KM_LEN]


def siv(km, data, tag_len=TAG_LEN):
	"""Compute the synthetic IV / MAC tag over ``data`` keyed with ``km``, truncated to ``tag_len``.

	Matches ``b2s_siv()``: a full 32-byte keyed BLAKE2s, of which the leftmost ``tag_len`` bytes are
	used as the tag.
	"""
	return hashlib.blake2s(data, key=km, digest_size=_DIGEST).digest()[:tag_len]


def _keystream(ke, iv, length):
	"""Generate ``length`` keystream bytes as BLAKE2s(key=ke, iv ‖ counter) blocks from counter 0."""
	out = bytearray()
	counter = 0
	iv = bytes(iv)
	while len(out) < length:
		block = hashlib.blake2s(iv + bytes([0, 0, 0, counter & 0xff]), key=ke, digest_size=_DIGEST)
		out += block.digest()
		counter += 1
	return bytes(out[:length])


def crypt(ke, iv, data):
	"""Encrypt or decrypt ``data`` (the operation is symmetric) with the keystream keyed by ``ke``.

	``iv`` is the SIV. Matches ``b2s_crypt()``.
	"""
	keystream = _keystream(ke, iv, len(data))
	return bytes(a ^ b for a, b in zip(data, keystream))
