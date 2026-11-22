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
	tools/battery-stats.py -f -e battery.log

With -e the graph is also exported as battery.svg and battery.png (the log name without its last extension), once
at start and then every 10 seconds while following.
"""

import argparse
import json
import logging
import os

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.ticker import MaxNLocator

# Topics are matched by their last level, so a topic prefix given to pynbus2-mq (-p) does not matter.
VOLTAGE = 'bat_voltage'
CURRENT = 'bat_current'
SOC = 'bat_soc'

# How often the followed file is checked for new lines, in milliseconds.
FOLLOW_INTERVAL_MS = 1000

# How often the graph is exported while following, in milliseconds.
EXPORT_INTERVAL_MS = 10000

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


def label(name, data, unit, decimals):
	"""A legend label showing the last value of the series ``data``, e.g. "voltage 3.912 V"."""
	_, y = data
	if not y:
		return f'{name} - {unit}'
	return f'{name} {y[-1]:.{decimals}f} {unit}'


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


def export(fig, base):
	"""Save the figure as <base>.svg and <base>.png. Each is written to a temporary file renamed over the old one,
	so a viewer never reads a partially written export."""
	for ext in ('svg', 'png'):
		path = f'{base}.{ext}'
		tmp = f'{path}.tmp'
		try:
			fig.savefig(tmp, format=ext)
			os.replace(tmp, path)
		except OSError as e:
			log.error('cannot export %s: %s', path, e)


def main():
	parser = argparse.ArgumentParser(description='Plot the battery voltage, current and state of charge from a '
	                                             'pynbus2-mq JSON log (pynbus2-mq -j).')
	parser.add_argument('log', type=str, help='the log file, one JSON object per line')
	parser.add_argument('-f', '--follow', action='store_true', help='follow the log as it is appended to')
	parser.add_argument('-e', '--export', action='store_true',
	                    help='export the graph as <log>.svg and <log>.png (the log name without its last extension), '
	                         'every 10 seconds while following')
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
	voltage, = left.plot([], [], color='tab:blue')
	current, = left.plot([], [], color='tab:orange')
	soc, = right.plot([], [], color='tab:green', linestyle='--')
	left.set_xlabel('time [s]')
	left.set_ylabel('voltage [V], current [A]')
	right.set_ylabel('SoC [%]')
	right.set_ylim(0, 100)
	# About 10 labelled ticks on the time axis, at round numbers of seconds. The time grid is independent of them:
	# a dashed gray line every minute, a gray line every 10 minutes and a black one every hour, drawn in redraw().
	left.xaxis.set_major_locator(MaxNLocator(nbins=10, steps=[1, 2, 3, 5, 6, 10]))
	left.grid(True, which='major', axis='y')
	grid = []
	fig.suptitle(args.log)

	def redraw():
		# Removed before rescaling, so the grid lines do not take part in the data limits.
		while grid:
			grid.pop().remove()
		voltage.set_data(*series.data[VOLTAGE])
		current.set_data(*series.data[CURRENT])
		soc.set_data(*series.data[SOC])
		left.relim()
		left.autoscale_view()
		right.relim()
		right.autoscale_view(scaley=False)

		# The legends show the last value received, the SoC one next to its axis on the right.
		voltage.set_label(label('voltage', series.data[VOLTAGE], 'V', 3))
		current.set_label(label('current', series.data[CURRENT], 'A', 3))
		soc.set_label(label('SoC', series.data[SOC], '%', 1))
		left.legend(handles=[voltage, current], loc='upper left')
		right.legend(handles=[soc], loc='upper right')

		for t in range(60, int(left.get_xlim()[1]) + 1, 60):
			if t % 3600 == 0:
				grid.append(left.axvline(t, color='black', linewidth=1.0, zorder=1))
			elif t % 600 == 0:
				grid.append(left.axvline(t, color='gray', linewidth=0.8, zorder=1))
			else:
				grid.append(left.axvline(t, color='gray', linewidth=0.8, linestyle='--', alpha=0.5, zorder=1))

	def update(frame):
		lines = read_lines(f, pending)
		for line in lines:
			series.add_line(line)
		if lines:
			redraw()

	redraw()
	base = os.path.splitext(args.log)[0]
	if args.export:
		export(fig, base)
	if args.follow:
		# Kept referenced until the window is closed, the animation and the timer stop when collected.
		animation = FuncAnimation(fig, update, interval=FOLLOW_INTERVAL_MS, cache_frame_data=False)
		if args.export:
			timer = fig.canvas.new_timer(interval=EXPORT_INTERVAL_MS)
			timer.add_callback(export, fig, base)
			timer.start()
	plt.show()
	f.close()


if __name__ == '__main__':
	main()
