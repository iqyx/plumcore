#!/usr/bin/env python3

import serial
import time
import logging
import sys
import struct
from colorama import init as colorama_init, Fore, Back, Style
from hashlib import blake2s
from secrets import token_bytes

colorama_init()
LOG_FORMAT = f"""[{Style.BRIGHT}{Fore.WHITE}%(asctime)s{Style.NORMAL}] {Fore.BLUE}{Style.BRIGHT}%(levelname)-10s\
{Fore.YELLOW}{Style.NORMAL}%(module)s:%(name)s: {Style.RESET_ALL}%(message)s"""


class Packet:

	key = b'abcd'

	def __init__(self, data):
		kd = blake2s(Packet.key).digest()
		self._ke = kd[:16]
		self._km = kd[16:]
		self._data = data

	def keystream(self, siv, l):
		ret = b''
		for i in range(l // 32 + 1):
			ret += blake2s(siv + struct.pack('>L', i), key=self._ke).digest()
		return ret[:l]

	def crypt(self, siv, m):
		ks = self.keystream(siv, len(m))
		return bytes([m[i] ^ ks[i] for i in range(len(m))])

	def header(self):
		return b'n2' + struct.pack('>H', len(self._data)) + b'\0\0\0\0'

	def id_header(self):
		return bytes([11, 12, 13, 14, 81, 82, 83, 84])

	def dumps(self):
		m = self.header() + self.id_header() + self._data
		siv = blake2s(m, key=self._km).digest()[:8]
		c = self.crypt(siv, m)
		return siv + c

s = serial.Serial('/dev/ttyUSB0', 1000000, timeout=0.01)
while True:
	tdata = Packet(token_bytes(1024)).dumps()
	# print(f'---> {tdata}')
	s.write(tdata)
	# time.sleep(0.001)
	rdata = s.read(1024)
	# print(f'<--- {rdata}')
	# time.sleep(0.1)
s.close()
