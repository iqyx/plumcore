#!/usr/bin/python

import os
import sys
import re
from profiler_messages import *

for m in re.findall("\[(.*?)\]", sys.stdin.read()):
	(message, time, param) = m.split(",")
	message_time = int(time, 16) / 10000.0;
	message_type = message[0:1]
	message_module = int(message[1:3], 16)
	message_msg = int(message[3:5], 16)

	print "module(%d) %s %s(%d) at %fs" % (
		message_module,
		message_type,
		profiler_messages[message_msg],
		int(param),
		message_time,
	)
