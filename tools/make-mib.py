#!/usr/bin/env python3

# SPDX-License-Identifier: GPL-3.0-or-later
#
# Generate a CBOR MIB image for the flash-cbor-mib service
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Build a CBOR MIB image consumable by the flash-cbor-mib service.

The image is a CBOR map describing a configuration tree, stored directly in a flash partition and
mirrored into the Conf tree by the device. The root is an indefinite-length map (initial byte 0xbf,
terminated by a 0xff break) whose mandatory first entry maps the magic key "CBORMIB" to the
structure version, e.g.:

    0xbf 0x67 "CBORMIB" 0x01 ... 0xff

Each further entry maps a node name (text-string key) to a value: a nested map (a subtree) or a
scalar whose configuration type is inferred from its CBOR encoding.

The content is read as a JSON object: from the file given by --in, or from stdin otherwise. A nested
object becomes a subtree; scalars map by their JSON type.

Content integrity is optional. When enabled, a top-level "check-blake2s" marker entry is added to
the map and a 32-byte blake2s digest of the whole map (0xbf head through 0xff break) is appended as
a trailer right after the break. The device keys off the marker and compares the digest; storing it
outside the hashed region avoids hashing the digest itself.
"""

import sys
import json
import argparse
import hashlib
import cbor2

# Magic first key of a MIB map; its value is the structure version the device reports.
MIB_MAGIC = 'CBORMIB'
MIB_VERSION = 1

# Top-level marker key selecting blake2s content verification; its value is an ignored placeholder.
CHECK_BLAKE2S = 'check-blake2s'


def init_args():
	parser = argparse.ArgumentParser(
		description="generate a CBOR MIB image for the flash-cbor-mib service",
		epilog="(c) 2026 Marek Koza <qyx@krtko.org>",
		formatter_class=argparse.RawDescriptionHelpFormatter
	)

	parser.add_argument('-i', '--in', dest='infile', type=str, default=None, help='input JSON content file (default: stdin)')
	parser.add_argument('-o', '--output', type=str, default='test-mib.bin', help='output MIB image file')
	parser.add_argument('-V', '--mib-version', type=int, default=MIB_VERSION, help='MIB structure version')
	parser.add_argument('--check', choices=['blake2s'], default=None, help='embed a content integrity check')

	return parser.parse_args()


def make_mib(content, version, check=None):
	"""Encode content as an indefinite-length CBOR map prefixed with the mandatory magic entry.

	cbor2 emits definite-length maps for dicts, so the root is assembled by hand to guarantee the
	0xbf head / 0xff break the device keys off. Nested maps may stay definite-length; the device
	accepts both. When check is 'blake2s', a "check-blake2s" marker is added to the map and the
	blake2s digest of the whole map is appended as a trailer after the break."""
	image = b'\xbf'
	image += cbor2.dumps(MIB_MAGIC) + cbor2.dumps(version)
	for name, value in content.items():
		image += cbor2.dumps(name) + cbor2.dumps(value)
	if check == 'blake2s':
		image += cbor2.dumps(CHECK_BLAKE2S) + cbor2.dumps(True)
	image += b'\xff'
	if check == 'blake2s':
		image += hashlib.blake2s(image, digest_size=32).digest()
	return image


if __name__ == "__main__":
	args = init_args()

	if args.infile is not None:
		with open(args.infile) as f:
			content = json.load(f)
	else:
		content = json.load(sys.stdin)

	image = make_mib(content, args.mib_version, check=args.check)

	with open(args.output, 'wb') as f:
		f.write(image)

	checked = f'{args.check} checked' if args.check else 'no integrity check'
	print(f'wrote {len(image)} byte MIB image (structure version {args.mib_version}, {checked}) to {args.output}')
