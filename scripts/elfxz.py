#!/usr/bin/env python3

import lzma
import argparse

def init_args():
	parser = argparse.ArgumentParser(
		description="plumCore ELF XZ compressor tool",
		epilog="(c) 2025 Marek Koza <qyx@krtko.org>",
		formatter_class=argparse.RawDescriptionHelpFormatter
	)

	parser.add_argument('--xz', type=str, default=None, help='specify ELF file to compress with XZ')

	return parser.parse_args()


if __name__ == "__main__":

	args = init_args()

	if args.xz:

		d = open(args.xz, 'rb').read()

		my_filters = [
			{"id": lzma.FILTER_ARMTHUMB},
			{"id": lzma.FILTER_LZMA2, "preset": 9, "dict_size": 8192},
		]

		c = lzma.LZMACompressor(format=lzma.FORMAT_XZ, filters=my_filters, check=lzma.CHECK_CRC32)
		hd = c.compress(d)
		hd = hd + c.flush()

		open(args.xz + '.xz', 'wb').write(hd)
