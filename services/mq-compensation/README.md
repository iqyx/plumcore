# mq-compensation

A message-queue service that applies a per-channel polynomial correction and a quadratic
temperature compensation to numeric measurements flowing through the message queue (MQ).

Each channel subscribes to an input topic, corrects every element of the received array in place
and republishes the result on an output topic, preserving the original dtype and shape. A single,
shared temperature topic feeds the temperature compensation of all channels.

## What it is for

Raw sensor readings (ADC counts, uncalibrated pressure, etc.) usually carry three kinds of error:

- an **offset** (the reading is non-zero when the true quantity is zero),
- a **gain** error (the slope of reading vs. true value is wrong), and
- **nonlinearity** (the reading-to-value relationship is curved).

On top of that, many sensors **drift with temperature**. This service corrects all of the above
with a polynomial (offset + gain + nonlinearity) followed by a quadratic temperature factor.

## The math

For a measured raw value `x` and a current temperature `T` (in °C), the compensated value `y` is
computed in two stages.

### 1. Value polynomial

The raw value is first centered around the calibration reference point `x_ref`:

```
xr = x - x_ref
```

and corrected by a polynomial of order up to `MQ_COMPENSATION_MAX_ORDER`:

```
y = c0 + c1·xr + c2·xr² + c3·xr³ + … + cN·xrᴺ
```

Interpreting the coefficients:

- `c0` absorbs the **offset** error (the value at the reference point),
- `c1` absorbs the **gain** error (the local slope),
- `c2 … cN` absorb the **nonlinearity**.

It is evaluated on the device with Horner's method for numerical stability:

```c
float xr = x - x_ref;
float y  = 0.0f;
for (int i = MAX_ORDER; i >= 0; i--) {
    y = y * xr + c[i];
}
```

Centering around `x_ref` keeps the float coefficients well conditioned around the operating point:
without centering, the high-order terms of `x` would span a huge dynamic range and lose precision.

### 2. Temperature compensation

The polynomial output is then divided by a quadratic temperature factor, centered around the
temperature reference `t_ref`:

```
tr = T - t_ref
tc = 1 + tc1·tr + tc2·tr²

y  = y / tc       (only if tc ≠ 0)
```

The factor is normalised to **1.0 at `t_ref`**, so:

- `tc1` is the **relative** deviation of the measured quantity per degree, and
- `tc2` is the relative deviation per degree squared.

Because the factor is 1.0 at the reference temperature, a sensor calibrated and operated at
`t_ref` is unaffected by this stage. Until a temperature is received on the temperature topic, the
service substitutes `MQ_COMPENSATION_DEFAULT_TEMP_C` (25.0 °C), which should match the temperature
at which the value polynomial was calibrated.

### Putting it together

```
y(x, T) = ( c0 + c1·(x - x_ref) + … + cN·(x - x_ref)ᴺ ) / ( 1 + tc1·(T - t_ref) + tc2·(T - t_ref)² )
```

## Configuration coefficients

Each channel exposes a configuration subtree named after the channel, under the service root
`compensation`. The leaves are:

| Node     | Meaning                                                        |
|----------|---------------------------------------------------------------|
| `x_ref`  | value polynomial reference point (raw units)                  |
| `c0`…`cN`| value polynomial coefficients (`N = MQ_COMPENSATION_MAX_ORDER`)|
| `t_ref`  | temperature reference point (°C)                              |
| `tc1`    | linear temperature coefficient (per °C)                       |
| `tc2`    | quadratic temperature coefficient (per °C²)                   |

Unused high-order coefficients should be left at `0.0`. Disabling temperature compensation is done
by setting `tc1 = tc2 = 0` (the factor stays 1.0 for all temperatures).

## Computing the coefficients

A helper script, [`tools/mq-comp-make-coeffs.py`](../../tools/mq-comp-make-coeffs.py), captures
measurements and fits both sets of coefficients for you. It reads `name=value` lines from a serial
port (or a captured log file) and prints the coefficients in the exact form expected by the service.

The two calibrations are independent and should be done separately:

1. The **value polynomial** must be captured at a stable temperature equal to `t_ref` (so the
   temperature factor is 1.0 and does not interfere with the fit).
2. The **temperature coefficients** must be captured on a constant input quantity while sweeping
   temperature.

### Value polynomial (`--linearize`)

Apply a series of known true values across the sensor's working range and let the script record the
averaged raw reading for each one. It then fits

```
true_value = c0 + c1·(raw - x_ref) + … + cN·(raw - x_ref)ᴺ
```

with `x_ref` defaulting to the mean of the recorded raw readings, and prints `x_ref` and `c0…cN`.

```
./tools/mq-comp-make-coeffs.py \
    --port /dev/ttyUSB0 \
    --value sensor/raw \
    --order 3 \
    --linearize 0 10 20 30 40 50
```

For each listed true value, let the live moving average settle and press Enter to record it. The
fit needs at least `order + 1` points; the default order is 3 and the maximum is 7
(`MQ_COMPENSATION_MAX_ORDER`). After fitting it prints the RMS and maximum residual and (if
matplotlib is available) plots the fit and residuals so you can judge whether the chosen order is
adequate.

### Temperature coefficients (`--tempco`)

Hold the input quantity constant and log the readings together with the temperature while the
temperature is swept across the operating range. Capture both the value and the temperature into a
log file in the same `name=value` format, then fit:

```
./tools/mq-comp-make-coeffs.py \
    --tempco \
    --file sweep.log \
    --value sensor/raw \
    --temp sensor/temp
```

The script pairs each value reading with the most recently seen temperature, fits the value against
temperature as `value(T) = a0 + a1·dT + a2·dT²` with `dT = T − t_ref`, and derives:

```
tc1 = a1 / a0
tc2 = a2 / a0
```

where `a0` is the value at `t_ref` (the normalisation point). `t_ref` defaults to the mean of the
captured temperatures and can be pinned with `--t-ref`. At least 3 `(temperature, value)` samples
are required for the quadratic fit. The script reports the residual error of the compensated value
and plots the measured value, the temperature model and the compensated value.

### Useful options

| Option            | Purpose                                                                  |
|-------------------|--------------------------------------------------------------------------|
| `--port`          | serial device, e.g. `/dev/ttyUSB0` (required except for `--tempco`)      |
| `--baud`          | serial baudrate (default 115200)                                         |
| `--value`         | parameter name carrying the measured value                              |
| `--temp`          | parameter name carrying the temperature (required by `--tempco`)        |
| `--window`        | moving-average window length in samples (default 16)                    |
| `--order`         | polynomial fit order (default 3, max 7)                                  |
| `--x-ref`         | pin the polynomial reference point (default: mean of recorded readings)  |
| `--t-ref`         | pin the temperature reference point (default: mean of captured temps)    |

Running with `--port` but without `--linearize` or `--tempco` enters a plain monitoring mode that
prints the running moving average — handy for confirming the parameter name and that data is
arriving before calibrating.

## Workflow summary

1. Stabilise the sensor at `t_ref`, run `--linearize` over a set of known true values, and note the
   printed `x_ref` and `c0…cN`.
2. Hold the input constant, sweep temperature while logging, run `--tempco` on the log, and note the
   printed `t_ref`, `tc1` and `tc2`.
3. Write all of the coefficients into the channel's configuration subtree under `compensation/`.
4. Point the service's temperature topic at the temperature source so the temperature factor is
   applied live.
