#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Convert pictures to C arrays usable by the fb-console service
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

import sys
import os
import argparse
import logging
from colorama import init as colorama_init, Fore, Style
from PIL import Image, ImageChops


# Output pixel formats matching enum fb_mode in services/interfaces/fb.h. Each entry maps the
# command line name to the C enum constant and the number of bits per pixel used when packing.
FORMATS = {
	'g1':      ('FB_MODE_G1', 1),
	'g2':      ('FB_MODE_G2', 2),
	'g4':      ('FB_MODE_G4', 4),
	'g8':      ('FB_MODE_G8', 8),
	'rgbx222': ('FB_MODE_RGBX222', 6),
	'rgb565':  ('FB_MODE_RGB565', 16),
	'rgb888':  ('FB_MODE_RGB888', 24),
}

DEFAULT_FORMAT = 'g1'


def resize_arg(value):
	"""Parse a WxH resize specification, e.g. 100x100."""
	try:
		w, h = value.lower().split('x')
		return (int(w), int(h))
	except ValueError:
		raise argparse.ArgumentTypeError(f'invalid resize "{value}", expected WxH, e.g. 100x100')


def init_args():
	parser = argparse.ArgumentParser(
		description="plumCore picture to fb-console C array converter",
		epilog="example: imtocdata.py --format g2 --out plum-pictures.inc plum.png\n\n(c) 2026 Marek Koza <qyx@krtko.org>",
		formatter_class=argparse.RawDescriptionHelpFormatter
	)

	parser.add_argument('input', type=str, help='input picture, any format supported by PIL')
	parser.add_argument('-f', '--format', type=str, choices=sorted(FORMATS.keys()), default=DEFAULT_FORMAT,
	                    help=f'output pixel format (default: {DEFAULT_FORMAT})')
	parser.add_argument('-o', '--out', type=str, help='write output to a file instead of stdout')
	parser.add_argument('-r', '--resize', type=resize_arg, metavar='WxH',
	                    help='resize the input to WxH (e.g. 100x100), ignoring aspect ratio')
	parser.add_argument('--debug', action='store_true', help='DEBUG level logging')
	parser.add_argument('--invert', action='store_true', help='invert the picture before conversion')

	return parser.parse_args()


def pixel_value(mode_name, bpp, r, g, b):
	"""Quantise an 8-bit-per-channel RGB pixel down to the target framebuffer mode.

	The quantisation matches color_to_native() in services/fb-painter/fb-painter.c so the
	generated arrays render identically to painter output.
	"""
	if mode_name in ('FB_MODE_G1', 'FB_MODE_G2', 'FB_MODE_G4', 'FB_MODE_G8'):
		# ITU-R BT.601 luma, then keep the top bits for the target depth.
		luma = (r * 77 + g * 150 + b * 29) >> 8
		return luma >> (8 - bpp)
	if mode_name == 'FB_MODE_RGBX222':
		return ((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6)
	if mode_name == 'FB_MODE_RGB565':
		return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
	if mode_name == 'FB_MODE_RGB888':
		return (r << 16) | (g << 8) | b
	raise ValueError(f'unsupported mode {mode_name}')


def pack_image(im, mode_name, bpp):
	"""Pack an image into the fb-console scanline layout.

	Sub-byte modes are packed MSB-first (leftmost pixel in the high bits), byte-multiple modes are
	stored big-endian. Each scanline is padded up to a whole number of bytes, matching
	fb_packed_size() = (w * bpp + 7) / 8 in fb-painter.
	"""
	rgb = im.convert('RGB')
	px = rgb.load()
	mask = (1 << bpp) - 1

	data = bytearray()
	for y in range(rgb.height):
		acc = 0
		bits = 0
		for x in range(rgb.width):
			r, g, b = px[x, y]
			acc = (acc << bpp) | (pixel_value(mode_name, bpp, r, g, b) & mask)
			bits += bpp
			while bits >= 8:
				bits -= 8
				data.append((acc >> bits) & 0xff)
		# Pad the tail of the scanline to a full byte if the pixels do not align.
		if bits > 0:
			data.append((acc << (8 - bits)) & 0xff)

	return data


def array_name(path):
	"""Derive the C identifier <basename>_data from an input file path."""
	base = os.path.splitext(os.path.basename(path))[0]
	ident = ''.join(c if c.isalnum() else '_' for c in base)
	if ident and ident[0].isdigit():
		ident = '_' + ident
	return f'{ident}_data'


def emit_image(out, path, mode_name, bpp, resize=None, invert=False):
	im = Image.open(path)
	name = array_name(path)

	if resize is not None:
		logging.debug(f'{path}: resizing from {im.width}x{im.height} to {resize[0]}x{resize[1]}')
		im = im.resize(resize)

	if invert:
		im = ImageChops.invert(im)

	if im.mode != 'RGB':
		logging.debug(f'{path}: converting from {im.mode} to {mode_name}')

	data = pack_image(im, mode_name, bpp)
	row_bytes = (im.width * bpp + 7) // 8

	out.write(f'const struct painter_raw_image {name} = {{\n')
	out.write(f'\t{im.width},\n')
	out.write(f'\t{im.height},\n')
	out.write(f'\t{mode_name},\n')
	out.write('\t{\n')
	for i in range(0, len(data), row_bytes):
		row = data[i:i + row_bytes]
		out.write('\t\t' + ', '.join('0x%02x' % b for b in row) + ',\n')
	out.write('\t}\n')
	out.write('};\n')

	logging.info(f'{path}: {im.width}x{im.height} {mode_name}, {name}, {len(data)} bytes')


def main():
	args = init_args()

	colorama_init()
	LOG_FORMAT = f"""[{Style.BRIGHT}{Fore.WHITE}%(asctime)s{Style.NORMAL}] {Fore.BLUE}{Style.BRIGHT}%(levelname)-10s\
{Fore.YELLOW}{Style.NORMAL}%(module)s:%(name)s: {Style.RESET_ALL}%(message)s"""

	logging.basicConfig(format=LOG_FORMAT, level=logging.DEBUG if args.debug else logging.INFO)

	mode_name, bpp = FORMATS[args.format]

	out = sys.stdout
	if args.out is not None:
		out = open(args.out, 'w')

	try:
		emit_image(out, args.input, mode_name, bpp, args.resize, args.invert)
	except (FileNotFoundError, OSError) as e:
		logging.error(str(e))
		return 1
	finally:
		if out is not sys.stdout:
			out.close()

	return 0


if __name__ == "__main__":
	sys.exit(main())
