#!/usr/bin/env python3

import z85
import json
import sys

def json_to_pkg(j):
	o = json.loads(j)
	if o.get('type') != 'BYTE':
		raise Exception('Wrong message type, expecting BYTE')
	# other fields (timestamp, topic) are not interesting
	return z85.decode(o.get('data'))[:o.get('size')]


for l in sys.stdin:
	sys.stdout.buffer.write(json_to_pkg(l))
