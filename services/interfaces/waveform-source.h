/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Waveform source interface
 *
 * Copyright (c) 2021-2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>


typedef enum {
	WAVEFORM_SOURCE_RET_OK = 0,
	WAVEFORM_SOURCE_RET_FAILED,
} waveform_source_ret_t;

/* All formats are in native endianness. */
enum waveform_source_format {
	WAVEFORM_SOURCE_FORMAT_U8,
	WAVEFORM_SOURCE_FORMAT_S8,
	WAVEFORM_SOURCE_FORMAT_U16,
	WAVEFORM_SOURCE_FORMAT_S16,
	WAVEFORM_SOURCE_FORMAT_U32,
	WAVEFORM_SOURCE_FORMAT_S32,
	WAVEFORM_SOURCE_FORMAT_FLOAT,
};

typedef struct waveform_source WaveformSource;
struct waveform_source_vmt {
	waveform_source_ret_t (*start)(WaveformSource *self);
	waveform_source_ret_t (*stop)(WaveformSource *self);
	waveform_source_ret_t (*read)(WaveformSource *self, void *data, size_t sample_count, size_t *read);
	waveform_source_ret_t (*set_format)(WaveformSource *self, enum waveform_source_format format, uint32_t channels);
	waveform_source_ret_t (*get_format)(WaveformSource *self, enum waveform_source_format *format, uint32_t *channels);
	waveform_source_ret_t (*set_sample_rate)(WaveformSource *self, float sample_rate_Hz);
	waveform_source_ret_t (*get_sample_rate)(WaveformSource *self, float *sample_rate_Hz);
};

typedef struct waveform_source {
	const struct waveform_source_vmt *vmt;
	void *parent;
} WaveformSource;
