#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Serial measurement linearization helper for the mq-compensation service
#
# Copyright (c) 2026, Marek Koza (qyx@krtko.org)
# All rights reserved.
#
# Reads "name=value" lines from a serial port and, in --linearize mode, walks through a list of
# known true values. For each one it keeps a moving average of the incoming measurements and, on
# Enter, records the averaged raw reading as the measurement corresponding to that true value. Once
# every point is collected it fits a polynomial mapping the raw reading to the true value, prints
# the c0..cN coefficients (and x_ref) expected by the mq-compensation service and shows the fit
# together with its residuals using matplotlib.
#
# In --tempco mode it instead reads a previously captured log --file (same "name=value" format) of
# a fixed quantity measured while sweeping temperature, fits the value against temperature and
# derives the t_ref/tc1/tc2 temperature compensation coefficients used by the service.

import argparse
import re
import sys
import threading
from collections import deque

import numpy as np
import serial

try:
	import matplotlib.pyplot as plt
except ImportError:
	plt = None


# Matches a single "name=value" assignment anywhere on a received line. The name may contain
# letters, digits, slashes, dots, dashes and underscores (eg. a hierarchical topic like
# "sensor/press"). The value accepts an optional sign, decimal point and exponent so both integer
# and floating point readings are parsed.
LINE_RE = re.compile(r"([A-Za-z0-9_./-]+)\s*=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)")


class SerialReader(threading.Thread):
	"""Background thread continuously parsing "name=value" lines and maintaining a moving average
	of the values seen for the selected parameter name."""

	def __init__(self, port, baud, name, window):
		super().__init__(daemon=True)
		self._serial = serial.Serial(port, baud, timeout=1.0)
		self._name = name
		self._values = deque(maxlen=window)
		self._lock = threading.Lock()
		self._total = 0
		self._stop = threading.Event()

	def run(self):
		while not self._stop.is_set():
			try:
				line = self._serial.readline().decode("ascii", errors="replace")
			except serial.SerialException:
				break
			if not line:
				continue
			for name, value in LINE_RE.findall(line):
				if self._name is not None and name != self._name:
					continue
				with self._lock:
					self._values.append(float(value))
					self._total += 1

	def average(self):
		"""Return the current moving average and the number of samples it covers, or (None, 0) when
		nothing has been received yet."""
		with self._lock:
			if not self._values:
				return None, 0
			return sum(self._values) / len(self._values), len(self._values)

	def total_samples(self):
		with self._lock:
			return self._total

	def stop(self):
		self._stop.set()
		self._serial.close()


def collect_points(reader, true_values, window):
	"""Walk through every true value, showing the live moving average until the user presses Enter
	to record the current reading. Returns parallel lists of measured and true values."""

	measured = []
	print("\nFor each target value let the moving average settle, then press Enter to record it.")
	print("Type 's' + Enter to skip a point, or 'q' + Enter to abort.\n")

	for index, true_value in enumerate(true_values):
		while True:
			avg, count = reader.average()
			if avg is None:
				prompt = f"[{index + 1}/{len(true_values)}] true={true_value:g}  measured=(waiting...) > "
			else:
				prompt = (f"[{index + 1}/{len(true_values)}] true={true_value:g}  "
					f"measured={avg:.6g} (avg of {count}/{window}) > ")

			try:
				command = input(prompt).strip().lower()
			except EOFError:
				command = "q"

			if command == "q":
				print("Aborted.")
				return None, None
			if command == "s":
				print(f"  skipped true={true_value:g}")
				break

			avg, count = reader.average()
			if avg is None:
				print("  no measurement received yet, waiting for data...")
				continue
			measured.append((avg, true_value))
			print(f"  recorded measured={avg:.6g} -> true={true_value:g}")
			break

	if not measured:
		return None, None
	xs = np.array([m for m, _ in measured], dtype=float)
	ys = np.array([t for _, t in measured], dtype=float)
	return xs, ys


def fit_polynomial(xs, ys, order, x_ref):
	"""Fit ys = c0 + c1*(xs - x_ref) + ... + cN*(xs - x_ref)^N and return the coefficients ordered
	c0..cN to match the mq-compensation service convention."""

	centered = xs - x_ref
	# numpy returns highest order first; reverse to get c0..cN.
	coeffs = np.polyfit(centered, ys, order)[::-1]
	return coeffs


def evaluate_polynomial(coeffs, xs, x_ref):
	"""Evaluate the c0..cN polynomial (same ordering as the service) at the raw values xs."""
	centered = xs - x_ref
	result = np.zeros_like(centered, dtype=float)
	for c in coeffs[::-1]:
		result = result * centered + c
	return result


def print_results(coeffs, x_ref, xs, ys):
	fitted = evaluate_polynomial(coeffs, xs, x_ref)
	residuals = ys - fitted
	rms = float(np.sqrt(np.mean(residuals ** 2)))
	max_abs = float(np.max(np.abs(residuals)))

	print("\n=== Linearization result ===")
	print(f"order   = {len(coeffs) - 1}")
	print(f"x_ref   = {x_ref:.8g}")
	for i, c in enumerate(coeffs):
		print(f"c{i}      = {c:.8g}")
	print(f"\nfit RMS error = {rms:.6g}, max abs error = {max_abs:.6g}")

	print("\nmq-compensation channel coefficients:")
	print(f"  x_ref {x_ref:.8g}")
	for i, c in enumerate(coeffs):
		print(f"  c{i} {c:.8g}")


def plot_results(coeffs, x_ref, xs, ys):
	if plt is None:
		print("\nmatplotlib not available, skipping plot.")
		return

	order = np.argsort(xs)
	xs_sorted = xs[order]
	dense = np.linspace(xs.min(), xs.max(), 400)
	fitted_dense = evaluate_polynomial(coeffs, dense, x_ref)
	residuals = ys - evaluate_polynomial(coeffs, xs, x_ref)

	fig, (ax_fit, ax_res) = plt.subplots(2, 1, figsize=(8, 8), sharex=True,
		gridspec_kw={"height_ratios": [3, 1]})

	ax_fit.plot(xs, ys, "o", label="recorded points")
	ax_fit.plot(dense, fitted_dense, "-", label=f"polynomial fit (order {len(coeffs) - 1})")
	ax_fit.set_ylabel("true value")
	ax_fit.set_title("mq-compensation linearization")
	ax_fit.grid(True)
	ax_fit.legend()

	ax_res.axhline(0.0, color="grey", linewidth=0.8)
	ax_res.plot(xs_sorted, residuals[order], "o-")
	ax_res.set_xlabel("measured (raw) value")
	ax_res.set_ylabel("residual")
	ax_res.grid(True)

	fig.tight_layout()
	plt.show()


def parse_pairs_file(path, value_name, temp_name):
	"""Parse a captured log file in the same "name=value" format as the serial stream and return
	paired (temperature, value) samples. Each value reading is paired with the most recently seen
	temperature so the two quantities may appear on the same line or on alternating lines."""
	temps = []
	values = []
	cur_temp = None
	with open(path, "r", errors="replace") as f:
		for line in f:
			seen_value = False
			cur_value = None
			for name, value in LINE_RE.findall(line):
				if name == temp_name:
					cur_temp = float(value)
				elif name == value_name:
					cur_value = float(value)
					seen_value = True
			if seen_value and cur_temp is not None:
				temps.append(cur_temp)
				values.append(cur_value)
	return np.array(temps, dtype=float), np.array(values, dtype=float)


def fit_tempco(temps, values, t_ref):
	"""Derive the temperature compensation coefficients for the mq-compensation service.

	The service divides the measured value by 1 + tc1*(T - t_ref) + tc2*(T - t_ref)^2, normalised to
	1.0 at t_ref. Fitting the measured value as value(T) = a0 + a1*dT + a2*dT^2 (with dT = T - t_ref)
	and dividing through by the value at t_ref (a0) gives that very factor, so tc1 = a1/a0 and
	tc2 = a2/a0. a0 is also returned as the value reference at t_ref."""
	dt = temps - t_ref
	a2, a1, a0 = np.polyfit(dt, values, 2)
	return a1 / a0, a2 / a0, a0


def tempco_factor(tc1, tc2, t_ref, temps):
	"""Evaluate the temperature compensation factor 1 + tc1*dT + tc2*dT^2 at the given temperatures."""
	dt = temps - t_ref
	return 1.0 + dt * (tc1 + dt * tc2)


def print_tempco_results(tc1, tc2, t_ref, y_ref, temps, values):
	corrected = values / tempco_factor(tc1, tc2, t_ref, temps)
	residuals = corrected - y_ref
	rms = float(np.sqrt(np.mean(residuals ** 2)))
	max_rel = float(np.max(np.abs(residuals)) / abs(y_ref)) if y_ref != 0.0 else float("nan")

	print("\n=== Temperature compensation result ===")
	print(f"samples = {len(temps)} over {temps.min():.4g}..{temps.max():.4g}")
	print(f"t_ref   = {t_ref:.8g}")
	print(f"y_ref   = {y_ref:.8g} (value at t_ref)")
	print(f"tc1     = {tc1:.8g}")
	print(f"tc2     = {tc2:.8g}")
	print(f"\ncorrected RMS error = {rms:.6g}, max relative error = {max_rel:.4g}")

	print("\nmq-compensation channel coefficients:")
	print(f"  t_ref {t_ref:.8g}")
	print(f"  tc1 {tc1:.8g}")
	print(f"  tc2 {tc2:.8g}")


def plot_tempco(tc1, tc2, t_ref, y_ref, temps, values):
	if plt is None:
		print("\nmatplotlib not available, skipping plot.")
		return

	order = np.argsort(temps)
	temps_sorted = temps[order]
	dense = np.linspace(temps.min(), temps.max(), 400)
	model_dense = y_ref * tempco_factor(tc1, tc2, t_ref, dense)
	corrected = values / tempco_factor(tc1, tc2, t_ref, temps)

	fig, (ax_fit, ax_res) = plt.subplots(2, 1, figsize=(8, 8), sharex=True,
		gridspec_kw={"height_ratios": [3, 1]})

	ax_fit.plot(temps, values, "o", label="measured value")
	ax_fit.plot(dense, model_dense, "-", label="temperature model")
	ax_fit.axvline(t_ref, color="grey", linewidth=0.8, linestyle="--", label=f"t_ref = {t_ref:g}")
	ax_fit.set_ylabel("measured value")
	ax_fit.set_title("mq-compensation temperature calibration")
	ax_fit.grid(True)
	ax_fit.legend()

	ax_res.axhline(y_ref, color="grey", linewidth=0.8)
	ax_res.plot(temps_sorted, corrected[order], "o-", label="compensated value")
	ax_res.set_xlabel("temperature")
	ax_res.set_ylabel("compensated value")
	ax_res.grid(True)
	ax_res.legend()

	fig.tight_layout()
	plt.show()


def monitor(reader, name):
	"""Plain monitoring mode: print every parsed value and the running moving average."""
	print("Monitoring serial port. Press Ctrl-C to stop.\n")
	try:
		while True:
			avg, count = reader.average()
			if avg is not None:
				label = name if name is not None else "value"
				sys.stdout.write(f"\r{label}: avg={avg:.6g} ({count} samples, {reader.total_samples()} total)   ")
				sys.stdout.flush()
			threading.Event().wait(0.2)
	except KeyboardInterrupt:
		print()


def main():
	parser = argparse.ArgumentParser(description=__doc__,
		formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--port", default=None,
						help="serial port device, eg. /dev/ttyUSB0 (required unless --tempco)")
	parser.add_argument("--baud", type=int, default=115200, help="serial baudrate (default 115200)")
	parser.add_argument("--value", default=None,
						help="parameter name carrying the measured value (default: any)")
	parser.add_argument("--temp", default=None,
						help="parameter name carrying the temperature (used by --tempco)")
	parser.add_argument("--window", type=int, default=16,
						help="moving average window length in samples (default 16)")
	parser.add_argument("--linearize", nargs="+", type=float, metavar="TRUE_VALUE",
						help="list of known true values to step through and calibrate against")
	parser.add_argument("--order", type=int, default=3,
						help="polynomial fit order (default 3, max 7 for mq-compensation)")
	parser.add_argument("--x-ref", type=float, default=None,
						help="polynomial reference point (default: mean of recorded measurements)")
	parser.add_argument("--tempco", action="store_true",
						help="compute temperature compensation coefficients from a captured --file")
	parser.add_argument("--file", default=None,
						help="captured 'name=value' log file with the temperature sweep (for --tempco)")
	parser.add_argument("--t-ref", type=float, default=None,
						help="temperature reference point (default: mean of the captured temperatures)")
	args = parser.parse_args()

	if args.window < 1:
		parser.error("--window must be at least 1")

	# Temperature compensation works off a captured file and needs no serial port.
	if args.tempco:
		if args.file is None:
			parser.error("--tempco requires --file")
		if args.value is None or args.temp is None:
			parser.error("--tempco requires both --value and --temp to identify the readings")
		temps, values = parse_pairs_file(args.file, args.value, args.temp)
		if len(temps) < 3:
			parser.error("need at least 3 (temperature, value) samples for the quadratic fit")
		t_ref = args.t_ref if args.t_ref is not None else float(np.mean(temps))
		tc1, tc2, y_ref = fit_tempco(temps, values, t_ref)
		print_tempco_results(tc1, tc2, t_ref, y_ref, temps, values)
		plot_tempco(tc1, tc2, t_ref, y_ref, temps, values)
		return

	if args.port is None:
		parser.error("--port is required for monitoring and --linearize")

	reader = SerialReader(args.port, args.baud, args.value, args.window)
	reader.start()

	try:
		if args.linearize is None:
			monitor(reader, args.value)
			return

		if args.order < 1:
			parser.error("--order must be at least 1")
		if args.order >= len(args.linearize):
			parser.error(f"--order {args.order} needs at least {args.order + 1} true values")

		xs, ys = collect_points(reader, args.linearize, args.window)
		if xs is None:
			return

		x_ref = args.x_ref if args.x_ref is not None else float(np.mean(xs))
		coeffs = fit_polynomial(xs, ys, args.order, x_ref)
		print_results(coeffs, x_ref, xs, ys)
		plot_results(coeffs, x_ref, xs, ys)
	finally:
		reader.stop()


if __name__ == "__main__":
	main()
