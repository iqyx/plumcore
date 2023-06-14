#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <waveform_source.h>

#include <interfaces/spi.h>
#include <interfaces/sensor.h>

typedef enum {
	/* Analog Devices ID, must read as 0xad */
	ADXL355_REG_DEVID_AD = 0x00,

	/* MEMS ID, must read as 0x1d */
	ADXL355_REG_DEVID_MST = 0x01,

	/* Device ID of the accelerometer, 0xed for ADXL355 */
	ADXL355_REG_PARTID = 0x02,

	/* ADXL355 revision ID, current is 0x01 */
	ADXL355_REG_REVID = 0x03,

	ADXL355_REG_STATUS = 0x04,
	#define ADXL355_REG_STATUS_NVM_BUSY (1 << 4)
	#define ADXL355_REG_STATUS_ACT (1 << 3)
	#define ADXL355_REG_STATUS_FIFO_OVR (1 << 2)
	#define ADXL355_REG_STATUS_FIFO_FULL (1 << 1)
	#define ADXL355_REG_STATUS_DATA_RDY (1 << 0)

	/* Number of valid samples in the FIFO, 0-96 */
	ADXL355_REG_FIFO_ENTRIES = 0x05,

	/* Uncalibrated temperature data, 4 MSB in bits 3:0 */
	ADXL355_REG_TEMP2 = 0x06,

	/* Uncalibrated temperature data, 8 LSB in bits 7:% */
	ADXL355_REG_TEMP1 = 0x07,

	/* X-axis acceleration data, 19:12 in XDATA3, 11:4 in XDATA2, 3:0 in XDATA1 */
	ADXL355_REG_XDATA1 = 0x08,
	ADXL355_REG_XDATA2 = 0x09,
	ADXL355_REG_XDATA3 = 0x0a,

	/* Y-axis acceleration data, 19:12 in YDATA3, 11:4 in YDATA2, 3:0 in YDATA1 */
	ADXL355_REG_YDATA1 = 0x0b,
	ADXL355_REG_YDATA2 = 0x0c,
	ADXL355_REG_YDATA3 = 0x0d,

	/* Z-axis acceleration data, 19:12 in ZDATA3, 11:4 in ZDATA2, 3:0 in ZDATA1 */
	ADXL355_REG_ZDATA1 = 0x0e,
	ADXL355_REG_ZDATA2 = 0x0f,
	ADXL355_REG_ZDATA3 = 0x10,

	/* Read this register to access FIFO data */
	ADXL355_REG_FIFO_DATA = 0x11,

	/* Offset added to X, Y, Z axis data. 2's complement value, 8 MSB is in the H register, 8 LSB is in the
	 * L register. The significance of offset 15:0 bits matches the significance of DATA register bits 19:4 */
	ADXL355_REG_OFFSET_X_H = 0x1e,
	ADXL355_REG_OFFSET_X_L = 0x1f,
	ADXL355_REG_OFFSET_Y_H = 0x20,
	ADXL355_REG_OFFSET_Y_L = 0x21,
	ADXL355_REG_OFFSET_Z_H = 0x22,
	ADXL355_REG_OFFSET_Z_L = 0x23,

	ADXL355_REG_ACT_EN = 0x24,
	#define ADXL355_REG_ACT_EN_X (1 << 0)
	#define ADXL355_REG_ACT_EN_Y (1 << 1)
	#define ADXL355_REG_ACT_EN_Z (1 << 2)

	ADXL355_REG_ACT_THRESH_H = 0x25,
	ADXL355_REG_ACT_THRESH_L = 0x26,
	ADXL355_REG_ACT_COUNT = 0x27,

	/* ODR, LPF and HPF settings */
	ADXL355_REG_FILTER = 0x28,
	ADXL355_REG_FIFO_SAMPLES = 0x29,
	ADXL355_REG_INT_MAP = 0x2a,
	ADXL355_REG_SYNC = 0x2b,
	ADXL355_REG_RANGE = 0x2c,
	ADXL355_REG_POWER_CTL = 0x2d,
	ADXL355_REG_SELF_TEST = 0x2e,
	ADXL355_REG_RESET = 0x2f,
} adxl355_reg_t;

typedef enum {
	ADXL355_RET_OK = 0,
	ADXL355_RET_FAILED = -1,
} adxl355_ret_t;

typedef struct {
	WaveformSource source;
	SpiDev *spidev;
	Sensor die_temp;
	uint8_t rev_id;
} Adxl355;


adxl355_ret_t adxl355_detect(Adxl355 *self);
adxl355_ret_t adxl355_activity_detect(Adxl355 *self, bool act_x, bool act_y, bool act_z, uint16_t threshold);
uint8_t adxl355_activity_count(Adxl355 *self);
uint32_t adxl355_fifo_entries(Adxl355 *self);
uint32_t adxl355_fifo_read_sample(Adxl355 *self);
adxl355_ret_t adxl355_init_defaults(Adxl355 *self);
adxl355_ret_t adxl355_init(Adxl355 *self, SpiDev *spi_dev);
adxl355_ret_t adxl355_free(Adxl355 *self);

/* WaveformSource API */
waveform_source_ret_t adxl355_start(Adxl355 *self);
waveform_source_ret_t adxl355_stop(Adxl355 *self);
waveform_source_ret_t adxl355_read(Adxl355 *self, void *data, size_t sample_count, size_t *read);
waveform_source_ret_t adxl355_get_format(Adxl355 *self, enum waveform_source_format *format, uint32_t *channels);
waveform_source_ret_t adxl355_set_sample_rate(Adxl355 *self, float sample_rate_Hz);
waveform_source_ret_t adxl355_get_sample_rate(Adxl355 *self, float *sample_rate_Hz);

