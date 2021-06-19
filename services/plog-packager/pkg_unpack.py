#!/usr/bin/env python3

import pkg_pb2
import heatshrink2
import sys
import mmap
import struct

def printProgressBar(iteration, total, prefix = '', suffix = '', decimals = 1, length = 100, fill = '#', printEnd = "\r"):
    """
    Call in a loop to create terminal progress bar
    @params:
        iteration   - Required  : current iteration (Int)
        total       - Required  : total iterations (Int)
        prefix      - Optional  : prefix string (Str)
        suffix      - Optional  : suffix string (Str)
        decimals    - Optional  : positive number of decimals in percent complete (Int)
        length      - Optional  : character length of bar (Int)
        fill        - Optional  : bar fill character (Str)
        printEnd    - Optional  : end character (e.g. "\r", "\r\n") (Str)
    """
    percent = ("{0:." + str(decimals) + "f}").format(100 * (iteration / float(total)))
    filledLength = int(length * iteration // total)
    bar = fill * filledLength + '-' * (length - filledLength)
    print(f'\r{prefix} |{bar}| {percent}% {suffix}', end = printEnd, file=sys.stderr)
    # Print New Line on Complete
    if iteration == total: 
        print()


def format_data(data, dtype):
	if dtype == pkg_pb2.Msg.Type.INT16:
		return struct.unpack("h" * (len(data) // 2), data)
	return []


def unpack_msg(topic='', dtype='', data=''):
	if topic_filter and topic != topic_filter:
		return
		
	print(topic, " ".join([str(i) for i in format_data(data, dtype)]))

def parse_pkg(pd):
	if pd[:3] != b'PKG':
		raise Exception('Invalid package magic')
	pd = pd[3:]
	rd = None

	p = pkg_pb2.Package()
	p.ParseFromString(pd)
	if p.data:
		pdata = pkg_pb2.PackageData()
		pdata.ParseFromString(p.data)
		# print(pdata)
		if pdata.heatshrink:
			rd = heatshrink2.decode(pdata.heatshrink.msg, window_sz2=pdata.heatshrink.window_size, lookahead_sz2=pdata.heatshrink.lookahead_size)
			if rd:
				rawdata = pkg_pb2.RawData()
				rawdata.ParseFromString(rd)
				for m in rawdata.msg:
					unpack_msg(m.topic, m.type, m.buf)


fname = sys.argv[1]
topic_filter = sys.argv[2]
with open(fname, "r+b") as f:
	with mmap.mmap(f.fileno(), 0) as mm:
		pkg_start = -1
		find_start = 0
		len_processed = 0
		prg_processed = 0
		printProgressBar(0, 1)
		while True:
			found = mm.find(b'PKG', find_start)
			if found >= 0:
				if pkg_start >= 0:
					pkg = mm[pkg_start:found]
					parse_pkg(pkg)

					len_processed += len(pkg)
					prg_processed += len(pkg)
					if prg_processed > 1e6:
						prg_processed = 0
						printProgressBar(len_processed, len(mm))
				pkg_start = found
				find_start = found + 3
			else:
				break
		printProgressBar(1, 1)
