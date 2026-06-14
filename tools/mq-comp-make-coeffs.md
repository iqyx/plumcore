# mq-comp-make-coeffs.py

Calibration helper that produces the coefficients consumed by the
[`mq-compensation`](../services/mq-compensation/README.md) service. It reads `name=value` lines
from a serial port (or a captured log file), fits the value polynomial and the temperature
compensation factor, and prints the coefficients in the exact form the service expects.

The script has three modes:

- **monitor** — print the running average of a parameter (sanity check before calibrating),
- **`--linearize`** — collect known true values interactively and fit the value polynomial,
- **`--tempco`** — fit the temperature compensation coefficients from a captured temperature sweep.

See the [service documentation](../services/mq-compensation/README.md) for the underlying math; this
page documents the tool itself.

## Requirements

- Python 3
- `numpy` and `pyserial` (required)
- `matplotlib` (optional — used only to plot the fit and residuals; the script still prints the
  coefficients without it)

## Input format

Every mode parses `name=value` assignments anywhere on a received (or logged) line. The name may be
a hierarchical topic such as `sensor/press` and may contain letters, digits, `_ . / -`. The value
accepts an optional sign, decimal point and exponent, so both integer and floating-point readings
are handled. Multiple assignments may appear on one line. Lines that do not match are ignored, so
the parser tolerates interleaved log noise.

Use `--value` to select which parameter is the measured value and (for `--tempco`) `--temp` to
select the temperature parameter. If `--value` is omitted in monitor/linearize mode, any parsed
name is accepted.

## Options

| Option            | Purpose                                                                  |
|-------------------|--------------------------------------------------------------------------|
| `--port`          | serial device, e.g. `/dev/ttyUSB0` (required except for `--tempco`)      |
| `--baud`          | serial baudrate (default 115200)                                         |
| `--value`         | parameter name carrying the measured value (default: any)               |
| `--temp`          | parameter name carrying the temperature (required by `--tempco`)        |
| `--window`        | moving-average window length in samples (default 16, must be ≥ 1)       |
| `--linearize V …` | list of known true values to step through and calibrate against         |
| `--order`         | polynomial fit order (default 3, max 7 for mq-compensation)             |
| `--x-ref`         | polynomial reference point (default: mean of recorded readings)         |
| `--tempco`        | compute temperature coefficients from a captured `--file`               |
| `--file`          | captured `name=value` log file with the temperature sweep (`--tempco`)  |
| `--t-ref`         | temperature reference point (default: mean of captured temperatures)    |

## Monitor mode

With `--port` but neither `--linearize` nor `--tempco`, the script prints the running moving average
of the selected parameter. Use it to confirm the parameter name and that data is arriving before
calibrating. Press Ctrl-C to stop.

```
./mq-comp-make-coeffs.py --port /dev/ttyUSB0 --value sensor/raw
```

## Value polynomial (`--linearize`)

Apply a series of **known true values** across the sensor's working range. For each one the script
shows a live moving average of the incoming raw readings; let it settle and press Enter to record
the averaged raw reading as the measurement corresponding to that true value.

```
./mq-comp-make-coeffs.py \
    --port /dev/ttyUSB0 \
    --value sensor/raw \
    --order 3 \
    --linearize 0 10 20 30 40 50
```

At each prompt:

- **Enter** — record the current moving average for this true value,
- **`s` + Enter** — skip this point,
- **`q` + Enter** — abort.

The script fits `true = c0 + c1·(raw − x_ref) + … + cN·(raw − x_ref)ᴺ`, with `x_ref` defaulting to
the mean of the recorded raw readings (override with `--x-ref`). It then prints `x_ref`, `c0…cN`,
the RMS and maximum residual, and plots the fit with its residuals.

Notes:

- The fit needs at least `order + 1` recorded points; the default order is 3 and the maximum
  accepted by the service is 7.
- Capture this calibration at a stable temperature equal to the temperature reference `t_ref` used
  for `--tempco`, so the temperature factor is 1.0 and does not bias the fit.

### Example output

```
=== Linearization result ===
order   = 3
x_ref   = 24.5
c0      = ...
c1      = ...
...

mq-compensation channel coefficients:
  x_ref 24.5
  c0 ...
  c1 ...
```

The block under "mq-compensation channel coefficients" maps directly onto the channel's
configuration nodes.

## Temperature coefficients (`--tempco`)

Hold the input quantity **constant** and log the readings together with the temperature while the
temperature is swept across the operating range. Capture both into a log file in the same
`name=value` format (this mode reads a file and needs no serial port).

```
./mq-comp-make-coeffs.py \
    --tempco \
    --file sweep.log \
    --value sensor/raw \
    --temp sensor/temp
```

Each value reading is paired with the most recently seen temperature, so the two quantities may
appear on the same line or on alternating lines. The script fits
`value(T) = a0 + a1·dT + a2·dT²` with `dT = T − t_ref` and derives `tc1 = a1/a0`, `tc2 = a2/a0`,
where `a0` is the value at `t_ref` (the normalisation point). `t_ref` defaults to the mean of the
captured temperatures (override with `--t-ref`).

At least 3 `(temperature, value)` samples are required for the quadratic fit. The script prints
`t_ref`, `tc1`, `tc2` and the compensated residual error, and plots the measured value, the
temperature model and the compensated value.

## Writing the coefficients back

The lines printed under "mq-compensation channel coefficients" correspond one-to-one with the
service's per-channel configuration nodes (`x_ref`, `c0…cN`, `t_ref`, `tc1`, `tc2`). After running
both calibrations, write all of them into the channel's subtree under `compensation/` and point the
service's temperature topic at the temperature source. See the
[service documentation](../services/mq-compensation/README.md) for the configuration layout.
