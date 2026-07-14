#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <waveform-source.h>

#include <interfaces/spi.h>
#include <interfaces/sensor.h>

typedef enum {
	IIS2ICLX_REG_WHO_AM_I = 0x0f,
	IIS2ICLX_REG_CTRL1_XL = 0x10,
	IIS2ICLX_REG_CTRL3_C = 0x12,
	IIS2ICLX_REG_CTRL9_XL = 0x18,
	IIS2ICLX_REG_OUT_TEMP_L = 0x20,
	IIS2ICLX_REG_OUT_TEMP_H = 0x21,
	IIS2ICLX_REG_OUTX_L_A = 0x28,
	IIS2ICLX_REG_OUTX_H_A = 0x29,
	IIS2ICLX_REG_OUTY_L_A = 0x2a,
	IIS2ICLX_REG_OUTY_H_A = 0x2b,
} iis2iclx_reg_t;

typedef enum {
	IIS2ICLX_RET_OK = 0,
	IIS2ICLX_RET_FAILED = -1,
} iis2iclx_ret_t;

typedef struct {
	WaveformSource source;
	SpiDev *spidev;
	Sensor die_temp;
	uint8_t rev_id;
} Iis2Iclx;


iis2iclx_ret_t iis2iclx_init(Iis2Iclx *self, SpiDev *spidev);
iis2iclx_ret_t iis2iclx_free(Iis2Iclx *self);
iis2iclx_ret_t iis2iclx_read(Iis2Iclx *self, int16_t *acc_x, int16_t *acc_y, float *temp);

/* WaveformSource API */
//waveform_source_ret_t adxl355_start(Adxl355 *self);
//waveform_source_ret_t adxl355_stop(Adxl355 *self);
//waveform_source_ret_t adxl355_read(Adxl355 *self, void *data, size_t sample_count, size_t *read);
//waveform_source_ret_t adxl355_get_format(Adxl355 *self, enum waveform_source_format *format, uint32_t *channels);
//waveform_source_ret_t adxl355_set_sample_rate(Adxl355 *self, float sample_rate_Hz);
//waveform_source_ret_t adxl355_get_sample_rate(Adxl355 *self, float *sample_rate_Hz);

