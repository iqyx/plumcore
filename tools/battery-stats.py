#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Plot the battery voltage, current and state of charge from a pynbus2-mq JSON log
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.

"""Plot the battery voltage and current (left axis) and the state of charge (right axis) over time from a log of
``pynbus2-mq -j`` output, one JSON object per line, e.g.::

	pynbus2-mq rible://nwdaq-hh1/mq -t "sensor/#" -j -a >> battery.log &
	tools/battery-stats.py -f battery.log
"""

import argparse
import json
import logging

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.ticker import MultipleLocator

# Topics are matched by their last level, so a topic prefix given to pynbus2-mq (-p) does not matter.
VOLTAGE = 'bat_voltage'
CURRENT = 'bat_current'
SOC = 'bat_soc'

# How often the followed file is checked for new lines, in milliseconds.
FOLLOW_INTERVAL_MS = 1000

log = logging.getLogger('battery-stats')


class Series:
	"""The samples of the plotted quantities read from the log so far. The time is in seconds since the first
	sample."""

	def __init__(self):
		self.data = {VOLTAGE: ([], []), CURRENT: ([], []), SOC: ([], [])}
		self.t0 = None

	def add_line(self, line):
		"""Add the sample carried by a single log line, if it is one of the plotted quantities."""
		try:
			v = json.loads(line)
			name = v['topic'].rsplit('/', 1)[-1]
			if name not in self.data or v['ts'] is None or v['value'] is None:
				return
			value = float(v['value'])
		except (ValueError, KeyError, TypeError, AttributeError):
			log.debug('skipping malformed line %r', line)
			return
		# The state of charge is published as a ratio, it is plotted in percent.
		if name == SOC:
			value *= 100.0
		if self.t0 is None:
			self.t0 = v['ts']
		t, y = self.data[name]
		t.append(v['ts'] - self.t0)
		y.append(value)


def read_lines(f, pending):
	"""Read the complete lines appended to ``f`` since the last call. A line not terminated yet (being written)
	is kept in ``pending[0]`` until the rest of it arrives."""
	lines = []
	while True:
		chunk = f.readline()
		if not chunk:
			return lines
		pending[0] += chunk
		if pending[0].endswith('\n'):
			lines.append(pending[0])
			pending[0] = ''


def main():
	parser = argparse.ArgumentParser(description='Plot the battery voltage, current and state of charge from a '
	                                             'pynbus2-mq JSON log (pynbus2-mq -j).')
	parser.add_argument('log', type=str, help='the log file, one JSON object per line')
	parser.add_argument('-f', '--follow', action='store_true', help='follow the log as it is appended to')
	parser.add_argument('--debug', action='store_true', help='DEBUG level logging')
	args = parser.parse_args()
	logging.basicConfig(level=logging.DEBUG if args.debug else logging.INFO)

	# All the text in Maple Mono Regular, 9 pt.
	plt.rcParams.update({
		'font.family': 'Maple Mono',
		'font.weight': 'normal',
		'font.size': 9,
		'axes.labelsize': 9,
		'axes.titlesize': 9,
		'figure.titlesize': 9,
		'legend.fontsize': 9,
		'xtick.labelsize': 9,
		'ytick.labelsize': 9,
	})

	series = Series()
	f = open(args.log, 'r')
	pending = ['']
	for line in read_lines(f, pending):
		series.add_line(line)

	fig, left = plt.subplots(figsize=(12, 6))
	right = left.twinx()
	voltage, = left.plot([], [], color='tab:blue', label='voltage [V]')
	current, = left.plot([], [], color='tab:orange', label='current [A]')
	soc, = right.plot([], [], color='tab:green', linestyle='--', label='SoC [%]')
	left.set_xlabel('time [s]')
	left.set_ylabel('voltage [V], current [A]')
	right.set_ylabel('SoC [%]')
	right.set_ylim(0, 100)
	# The time grid: a dashed gray line every minute, a gray line every 10 minutes (labelled) and a black one
	# every hour, drawn in redraw() as the axes have only two tick levels.
	left.xaxis.set_major_locator(MultipleLocator(600))
	left.xaxis.set_minor_locator(MultipleLocator(60))
	left.grid(True, which='major', axis='x', color='gray', linestyle='-')
	left.grid(True, which='minor', axis='x', color='gray', linestyle='--', alpha=0.5)
	left.grid(True, which='major', axis='y')
	hours = []
	left.legend(handles=[voltage, current, soc], loc='upper left')
	fig.suptitle(args.log)

	def redraw():
		voltage.set_data(*series.data[VOLTAGE])
		current.set_data(*series.data[CURRENT])
		soc.set_data(*series.data[SOC])
		left.relim()
		left.autoscale_view()
		right.relim()
		right.autoscale_view(scaley=False)
		while hours:
			hours.pop().remove()
		end = left.get_xlim()[1]
		hours.extend(left.axvline(h, color='black', linewidth=1.0, zorder=1) for h in range(3600, int(end) + 1, 3600))

	def update(frame):
		lines = read_lines(f, pending)
		for line in lines:
			series.add_line(line)
		if lines:
			redraw()

	redraw()
	if args.follow:
		# Kept referenced until the window is closed, the animation stops when collected.
		animation = FuncAnimation(fig, update, interval=FOLLOW_INTERVAL_MS, cache_frame_data=False)
	plt.show()
	f.close()


if __name__ == '__main__':
	main()
