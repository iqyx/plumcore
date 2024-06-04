#!/usr/bin/python3

import colorama
import argparse
from textwrap import dedent
from colorama import init as colorama_init, Fore, Back, Style


parser = argparse.ArgumentParser(
	description=dedent("""
		Colourful memory diff tool
	"""),
	epilog="(c) 2023 Marek Koza <qyx@krtko.org>",
	formatter_class=argparse.RawDescriptionHelpFormatter
)

# Generic parameters
parser.add_argument('a', type=str)
parser.add_argument('b', type=str)
parser.add_argument('-g', '--granularity', type=int, default=16, help='Granularity of comparison (the smallest chunk in the resulting graph)')
parser.add_argument('-w', '--wide', type=int, default=1, help='Generate wide graph (2 or 4 squares)')
parser.add_argument('--delimiter', type=str, default='  ')
parser.add_argument('--space', type=str, default=' ')
parser.add_argument('--hl', type=str, default=[], nargs='+')



args = parser.parse_args()
highlights = []
for hl in args.hl:
	(addr, size) = hl.split(':', 2)
	highlights.append((int(addr, 0), int(size, 0)))

colorama_init()


def gen_diff(addr, size):
	nbytes = 0
	for i in range(size):
		if content_a[addr + i] != content_b[addr + i]:
			nbytes += 1

	bg = ''
	for (hl_addr, hl_size) in highlights:
		if addr >= hl_addr and addr < (hl_addr + hl_size):
			bg = Back.LIGHTBLACK_EX

	if nbytes == 0:
		return [f'{bg}{Fore.WHITE}.{Style.RESET_ALL}']
	elif (nbytes / size) <= 0.1:
		return [f'{bg}{Fore.BLUE}{Style.BRIGHT}o{Style.RESET_ALL}']
	elif (nbytes / size) <= 0.5:
		return [f'{bg}{Fore.GREEN}{Style.BRIGHT}o{Style.RESET_ALL}']
	elif (nbytes / size) < 1:
		return [f'{bg}{Fore.YELLOW}{Style.BRIGHT}o{Style.RESET_ALL}']
	else:
		return [f'{bg}{Fore.RED}{Style.BRIGHT}o{Style.RESET_ALL}']


def gen_split(addr, size, minsize, wide):
	if size <= minsize:
		r = gen_diff(addr, size)
	else:
		if wide == 2:
			size //= 2
			r =  [a + args.delimiter + b for a, b in zip(gen_split(addr, size, minsize, 1), gen_split(addr + size, size, minsize, 1))]
		elif wide == 4:
			size //= 4
			r1 =  [a + args.delimiter + b for a, b in zip(gen_split(addr, size, minsize, 1), gen_split(addr + size, size, minsize, 1))]
			r2 = [a + args.delimiter + b for a, b in zip(gen_split(addr + size * 2, size, minsize, 1), gen_split(addr + size * 3, size, minsize, 1))]
			r = [a + args.delimiter + b for a, b in zip(r1, r2)]
		else:
			size //= 4
			r =  [a + args.space + b for a, b in zip(gen_split(addr, size, minsize, 1), gen_split(addr + size, size, minsize, 1))]
			r += [a + args.space + b for a, b in zip(gen_split(addr + size * 2, size, minsize, 1), gen_split(addr + size * 3, size, minsize, 1))]

	return r


content_a = open(args.a, 'rb').read()
content_b = open(args.b, 'rb').read()

for l in gen_split(0, len(content_a), args.granularity, args.wide):
	print(l)

