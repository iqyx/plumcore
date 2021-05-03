#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <waveform_source.h>

#include "interface_spidev.h"

typedef enum {
	ICM42688P_REG_DEVICE_CONFIG = 0x11,
	ICM42688P_REG_DRIVE_CONFIG = 0x13,
	ICM42688P_REG_INT_CONFIG = 0x14,
	ICM42688P_REG_FIFO_CONFIG = 0x16,
	ICM42688P_REG_FIFO_COUNTH = 0x2e,
	ICM42688P_REG_FIFO_COUNTL = 0x2f,
	ICM42688P_REG_FIFO_DATA = 0x30,
	ICM42688P_REG_INTF_CONFIG0 = 0x4c,
	ICM42688P_REG_INTF_CONFIG1 = 0x4d,
	ICM42688P_REG_PWR_MGMT0 = 0x4e,
	ICM42688P_REG_GYRO_CONFIG0 = 0x4f,
	ICM42688P_REG_ACCEL_CONFIG0 = 0x50,
	ICM42688P_REG_GYRO_ACCEL_CONFIG0 = 0x52,
	ICM42688P_REG_ACCEL_UI_FILT_ORD = 0x53,
	ICM42688P_REG_TMST_CONFIG = 0x54,
	ICM42688P_REG_FIFO_CONFIG1 = 0x5F,
	ICM42688P_REG_FIFO_CONFIG2 = 0x60,
	ICM42688P_REG_FIFO_CONFIG3 = 0x61,
	ICM42688P_REG_FSYNC_CONFIG = 0x62,
	ICM42688P_REG_WHO_AM_I = 0x75,
	ICM42688P_REG_SEL_BANK = 0x76,

	ICM42688P_REG_INTF_CONFIG5 = 0x7b,
} icm42688p_reg_t;

typedef enum {
	ICM42688P_RET_OK = 0,
	ICM42688P_RET_FAILED = -1,
} icm42688p_ret_t;

typedef struct {
	WaveformSource source;
	struct interface_spidev *spidev;
	uint8_t who_am_i;
} Icm42688p;


icm42688p_ret_t icm42688p_detect(Icm42688p *self);
uint16_t icm42688p_fifo_count(Icm42688p *self);
icm42688p_ret_t icm42688p_init_defaults(Icm42688p *self);
icm42688p_ret_t icm42688p_init(Icm42688p *self, struct interface_spidev *spi_dev);
icm42688p_ret_t icm42688p_free(Icm42688p *self);

/* WaveformSource API (Icm42688p.source) */
waveform_source_ret_t icm42688p_read(Icm42688p *self, void *data, size_t sample_count, size_t *read);
waveform_source_ret_t icm42688p_set_format(void *parent, enum waveform_source_format format, uint32_t channels);
waveform_source_ret_t icm42688p_get_format(void *parent, enum waveform_source_format *format, uint32_t *channels);
waveform_source_ret_t icm42688p_set_sample_rate(void *parent, float sample_rate_Hz);
waveform_source_ret_t icm42688p_get_sample_rate(void *parent, float *sample_rate_Hz);

