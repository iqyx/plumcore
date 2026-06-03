/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Measurement data interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef enum {
	MEASUREMENT_RET_OK = 0,
	MEASUREMENT_RET_FAILED,
} measurement_ret_t;

/* Type of the value carried by a Measurement. It selects the active union member below. bstr is a byte string. */
enum measurement_type {
	MEASUREMENT_TYPE_U8 = 0,
	MEASUREMENT_TYPE_S8,
	MEASUREMENT_TYPE_U16,
	MEASUREMENT_TYPE_S16,
	MEASUREMENT_TYPE_U32,
	MEASUREMENT_TYPE_S32,
	MEASUREMENT_TYPE_F32,
	MEASUREMENT_TYPE_F64,
	MEASUREMENT_TYPE_BSTR,
};

/* The value itself, always referenced by a pointer regardless of whether it is a single sample or an array. The
 * matching member (selected by @p type) points to the first of @p size elements; a single scalar is simply @p size
 * == 1. For bstr the pointer addresses a byte string of @p size bytes. */
union measurement_value {
	uint8_t *u8;
	int8_t *s8;
	uint16_t *u16;
	int16_t *s16;
	uint32_t *u32;
	int32_t *s32;
	float *f32;
	double *f64;
	uint8_t *bstr;
};

/* Selects how the timestamps of the individual samples in a measurement array are derived. Irrelevant for a single
 * scalar (size == 1). */
enum measurement_timebase {
	/* No per-sample timebase; @p timestamp applies to the value as a whole. */
	MEASUREMENT_TIMEBASE_NONE = 0,
	/* Samples are equally spaced in time; @p period gives the interval between two consecutive samples. */
	MEASUREMENT_TIMEBASE_PERIODIC,
	/* Each sample carries its own relative time offset from the previous one; @p offsets points to an array of
	 * @p size durations (the offset of the first sample is measured from @p timestamp). */
	MEASUREMENT_TIMEBASE_OFFSETS,
};

/* Environmental conditions under which the measurement was taken. Referenced from a Measurement so several
 * measurements can share a single record. */
struct measurement_environment {
	/* Ambient temperature in degrees Celsius. */
	float temperature;

	/* Relative humidity in percent (0..100). */
	float humidity;

	/* Atmospheric pressure in pascals. */
	float pressure;
};

/* Unlike a regular interface, a data interface carries no virtual method table. It is a plain structured-data
 * holder (a "data class") with helper functions to initialise, manipulate and release it. */
typedef struct measurement {
	/* Name identifying the value/array, eg. "temperature" or "ch0". Optional, may be NULL. */
	const char *name;

	/* The measured value together with the type selecting the active union member. */
	enum measurement_type type;
	union measurement_value value;

	/* Number of elements @p value points to: 1 for a single scalar, > 1 for an array (for bstr, the length in
	 * bytes). 0 means no value. */
	size_t size;

	/* Measurement uncertainty describing the value: a symmetric ± half-width of the same type as the value (see
	 * @p type), referenced by a pointer to either a single value or @p size per-sample values. @p uncertainty_k is
	 * the coverage factor (number of standard deviations the half-width represents); NULL or k == 0 means the
	 * uncertainty is unknown / not provided. */
	union measurement_value uncertainty;
	float uncertainty_k;

	/* Basic statistics over a measurement array, each of the same type as the value (see @p type) and referenced by
	 * a pointer to a single element. Optional, each may be NULL. */
	union measurement_value min;
	union measurement_value max;
	union measurement_value mean;

	/* Moment the value was sampled. For arrays it is the time base the per-sample timestamps are derived from. */
	struct timespec timestamp;

	/* Uncertainty of @p timestamp as a symmetric ± duration (absolute time accuracy). */
	struct timespec timestamp_uncertainty;

	/* Precision (stability) of the time base in parts per billion, eg. the oscillator tolerance used to derive the
	 * per-sample timestamps. */
	uint32_t timestamp_precision_ppb;

	/* How the timestamps of array samples are derived from @p timestamp. */
	enum measurement_timebase timebase;

	/* Interval between two consecutive samples, valid for MEASUREMENT_TIMEBASE_PERIODIC. */
	struct timespec period;

	/* Relative time offsets of the individual samples, valid for MEASUREMENT_TIMEBASE_OFFSETS. Points to an array
	 * of @p size durations matching the value array. */
	const struct timespec *offsets;

	/* Human-readable description of the measurement, eg. "die temperature". Optional, may be NULL. */
	const char *description;

	/* Physical unit of the value, both spelled out and abbreviated, eg. "degree Celsius" / "°C". Optional, each
	 * may be NULL. */
	const char *unit_name;
	const char *unit_abbr;

	/* Environmental conditions the measurement was taken under. Optional, may be NULL. */
	const struct measurement_environment *environment;
} Measurement;

measurement_ret_t measurement_init(Measurement *self);
measurement_ret_t measurement_free(Measurement *self);
