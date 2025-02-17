/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Waveform sink interface
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>


typedef enum {
	WAVEFORM_SINK_RET_OK = 0,
	WAVEFORM_SINK_RET_FAILED,
} waveform_sink_ret_t;

/* All formats are in native endianness. */
enum waveform_sink_format {
	WAVEFORM_SINK_FORMAT_U8,
	WAVEFORM_SINK_FORMAT_S8,
	WAVEFORM_SINK_FORMAT_U16,
	WAVEFORM_SINK_FORMAT_S16,
	WAVEFORM_SINK_FORMAT_U32,
	WAVEFORM_SINK_FORMAT_S32,
	WAVEFORM_SINK_FORMAT_FLOAT,
};

typedef struct waveform_sink WaveformSink;
struct waveform_sink_vmt {
	waveform_sink_ret_t (*start)(WaveformSink *self);
	waveform_sink_ret_t (*stop)(WaveformSink *self);
	waveform_sink_ret_t (*write)(WaveformSink *self, void *data, size_t sample_count);
	waveform_sink_ret_t (*set_format)(WaveformSink *self, enum waveform_sink_format format, uint32_t channels);
	waveform_sink_ret_t (*get_format)(WaveformSink *self, enum waveform_sink_format *format, uint32_t *channels);
	waveform_sink_ret_t (*set_sample_rate)(WaveformSink *self, float sample_rate_Hz);
	waveform_sink_ret_t (*get_sample_rate)(WaveformSink *self, float *sample_rate_Hz);
};

typedef struct waveform_sink {
	const struct waveform_sink_vmt *vmt;
	void *parent;
} WaveformSink;





