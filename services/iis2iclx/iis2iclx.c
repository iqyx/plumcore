#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <main.h>

#include <interfaces/spi.h>
#include <interfaces/waveform-source.h>

#include "iis2iclx.h"

//#define DEBUG
#define MODULE_NAME "adxl355"


static uint8_t read8(Iis2Iclx *self, iis2iclx_reg_t addr) {
	uint8_t txbuf = 0x80 | (addr & 0x7f);
	self->spidev->vmt->select(self->spidev);
	self->spidev->vmt->send(self->spidev, &txbuf, 1);

	uint8_t rxbuf = 0;
	self->spidev->vmt->receive(self->spidev, &rxbuf, 1);
	self->spidev->vmt->deselect(self->spidev);

	#if defined(DEBUG)
		u_log(system_log, LOG_TYPE_DEBUG, "read8 [0x%02x] = 0x%02x", addr, rxbuf);
	#endif
	return rxbuf;
}


static int16_t read16i(Iis2Iclx *self, iis2iclx_reg_t addr) {
	uint8_t txbuf = 0x80 | (addr & 0x7f);
	self->spidev->vmt->select(self->spidev);
	self->spidev->vmt->send(self->spidev, &txbuf, 1);

	int16_t rxbuf = 0;
	self->spidev->vmt->receive(self->spidev, (uint8_t *)&rxbuf, 2);
	self->spidev->vmt->deselect(self->spidev);

	#if defined(DEBUG)
		u_log(system_log, LOG_TYPE_DEBUG, "read16i [0x%02x] = %d", addr, rxbuf);
	#endif
	return rxbuf;
}



static void write8(Iis2Iclx *self, iis2iclx_reg_t addr, uint8_t value) {
	uint8_t txbuf[2] = {addr, value};
	self->spidev->vmt->select(self->spidev);
	self->spidev->vmt->send(self->spidev, txbuf, 2);
	self->spidev->vmt->deselect(self->spidev);
	#if defined(DEBUG)
		u_log(system_log, LOG_TYPE_DEBUG, "write8 [0x%02x] = 0x%02x", addr, value);
	#endif
}



/**********************************************************************************************************************
 * Waveform source API
 **********************************************************************************************************************/

#if 0
waveform_source_ret_t adxl355_start(Adxl355 *self) {
	write8(self, ADXL355_REG_POWER_CTL, 0x00);
	vTaskDelay(50);
	return WAVEFORM_SOURCE_RET_OK;
}


waveform_source_ret_t adxl355_stop(Adxl355 *self) {
	/* Enable standby mode. */
	write8(self, ADXL355_REG_POWER_CTL, 0x01);
	vTaskDelay(50);
	return WAVEFORM_SOURCE_RET_OK;
}


waveform_source_ret_t adxl355_read(Adxl355 *self, void *data, size_t sample_count, size_t *read) {
	/* Work with the read buffer as with unsigned 32 bit ints. */
	uint32_t *data32 = data;
	int32_t *sdata32 = data;

	/* Get the number of samples to read. Expect full triplets. */
	uint32_t fifo_count = adxl355_fifo_entries(self) / 3;
	if (sample_count > fifo_count) {
		sample_count = fifo_count;
	}

	#if defined(DEBUG)
		u_log(system_log, LOG_TYPE_DEBUG, "ws_read(data=0x%08x, count=%d, read=0x%08x)", data, sample_count, read);
	#endif

	*read = 0;
	while (sample_count > 0) {
		/* Attempt to read the X axis data. Check whether we are in sync
		 * (the last bit needs to be 1 for the X axis). Read the X axis again if not. */
		uint32_t ax = adxl355_fifo_read_sample(self);
		if ((ax & 1UL) == 0) {
			continue;
		}
		uint32_t ay = adxl355_fifo_read_sample(self);
		uint32_t az = adxl355_fifo_read_sample(self);

		#if defined(DEBUG)
			u_log(system_log, LOG_TYPE_DEBUG, "sample ax=%lu ay=%lu az=%lu", ax, ay, az);
		#endif

		/* The sync bit is processed now. Check if we have read past the end by any chance. */
		if ((ax & 2UL) || (ay & 2UL) || (az & 2UL)) {
			break;
		}

		/* Everything ok now, consolidate the data and copy to the provided buffer.
		 * Accelerometer samples are 24 bit now saved in a 32 bit unsigned int. The sign bit is bit 23.
		 * Shift the sample 8 bits to the left, do not treat the sign in any way but advertise
		 * the buffer as being *signed* 32 bit. Mask the 4 LSB. */
		data32[0] = (ax << 8) & 0xfffff000;
		data32[1] = (ay << 8) & 0xfffff000;
		data32[2] = (az << 8) & 0xfffff000;

		sdata32[0] >>= 12;
		sdata32[1] >>= 12;
		sdata32[2] >>= 12;

		/* Advance to the next position. */
		data32 += 3;
		sample_count--;

		(*read)++;
	}
	#if defined(DEBUG)
		u_log(system_log, LOG_TYPE_DEBUG, "ws_read return");
	#endif

	return WAVEFORM_SOURCE_RET_OK;
}


waveform_source_ret_t adxl355_get_format(Adxl355 *self, enum waveform_source_format *format, uint32_t *channels) {
	*format = WAVEFORM_SOURCE_FORMAT_S32;
	*channels = 3;
	return WAVEFORM_SOURCE_RET_OK;
}


waveform_source_ret_t adxl355_set_sample_rate(Adxl355 *self, float sample_rate_Hz) {


}


waveform_source_ret_t adxl355_get_sample_rate(Adxl355 *self, float *sample_rate_Hz) {


}

#endif


/**********************************************************************************************************************
 * Die temperature sensor Sensor API
 **********************************************************************************************************************/

#if 0
static sensor_ret_t die_temp_sensor_value_f(Sensor *sensor, float *value) {
	Adxl355 *self = (Adxl355 *)sensor->parent;

	float f = 0.0f;

	uint8_t lo1, lo2, hi;
	do {
		lo1 = read8(self, ADXL355_REG_TEMP1);
		hi = read8(self, ADXL355_REG_TEMP2);
		lo2 = read8(self, ADXL355_REG_TEMP1);
	} while (lo1 != lo2);
	f = ((hi << 8) | lo1) & 0xfff;

	/* Apply the conversion */
	f = (2111.25f - f) / 9.05f;

	if (value != NULL) {
		*value = f;
		return SENSOR_RET_OK;
	}

	return SENSOR_RET_FAILED;
}


static const struct sensor_vmt die_temp_sensor_vmt = {
	.value_f = die_temp_sensor_value_f,
};


const struct sensor_info die_temp_sensor_info = {
	.description = "accelerometer die temperature",
	.unit = "°C",
};
#endif



iis2iclx_ret_t iis2iclx_init(Iis2Iclx *self, SpiDev *spidev) {
	memset(self, 0, sizeof(Iis2Iclx));
	self->spidev = spidev;

	uint8_t who_am_i = read8(self, IIS2ICLX_REG_WHO_AM_I);
	u_log(system_log, LOG_TYPE_DEBUG, "who_am_i = 0x%02x", who_am_i);

	/* Reset. */
	write8(self, IIS2ICLX_REG_CTRL3_C, 0x01);
	vTaskDelay(5);
	write8(self, IIS2ICLX_REG_CTRL1_XL, 0x50);
	write8(self, IIS2ICLX_REG_CTRL9_XL, 0xf1);

/*
	waveform_source_init(&self->source);
	self->source.parent = self;
	self->source.start = (typeof(self->source.start))adxl355_start;
	self->source.stop= (typeof(self->source.stop))adxl355_stop;
	self->source.read = (typeof(self->source.read))adxl355_read;
	self->source.get_format = (typeof(self->source.get_format))adxl355_get_format;
	self->source.get_sample_rate = (typeof(self->source.get_sample_rate))adxl355_get_sample_rate;
	self->source.set_sample_rate = (typeof(self->source.set_sample_rate))adxl355_set_sample_rate;

	self->die_temp.vmt = &die_temp_sensor_vmt;
	self->die_temp.info = &die_temp_sensor_info;
	self->die_temp.parent = self;
*/

	return IIS2ICLX_RET_OK;
}


iis2iclx_ret_t iis2iclx_free(Iis2Iclx *self) {
	return IIS2ICLX_RET_OK;
}


iis2iclx_ret_t iis2iclx_read(Iis2Iclx *self, int16_t *acc_x, int16_t *acc_y, float *temp) {

	*acc_x = read16i(self, IIS2ICLX_REG_OUTX_L_A);
	*acc_y = read16i(self, IIS2ICLX_REG_OUTY_L_A);

	/* The temperature sensor outputs a signed 16-bit value with a sensitivity of 256 LSB/°C and a
	 * nominal output of 0 LSB at 25 °C. Convert the raw reading to degrees Celsius. */
	*temp = (float)read16i(self, IIS2ICLX_REG_OUT_TEMP_L) / 256.0f + 25.0f;

	return IIS2ICLX_RET_OK;
}


