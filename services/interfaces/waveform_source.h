/*
 * Waveform source interface
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "interface.h"


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

typedef struct {
	Interface interface;

	void *parent;
	waveform_source_ret_t (*start)(void *parent);
	waveform_source_ret_t (*stop)(void *parent);
	waveform_source_ret_t (*read)(void *parent, void *data, size_t sample_count, size_t *read);
	waveform_source_ret_t (*set_format)(void *parent, enum waveform_source_format format, uint32_t channels);
	waveform_source_ret_t (*get_format)(void *parent, enum waveform_source_format *format, uint32_t *channels);
	waveform_source_ret_t (*set_sample_rate)(void *parent, float sample_rate_Hz);
	waveform_source_ret_t (*get_sample_rate)(void *parent, float *sample_rate_Hz);
	

} WaveformSource;


waveform_source_ret_t waveform_source_init(WaveformSource *self);
waveform_source_ret_t waveform_source_free(WaveformSource *self);



