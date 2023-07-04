#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"

#include <interfaces/spi.h>
#include "icm42688p.h"
#include "u_log.h"
#include "u_assert.h"
#include <waveform_source.h>

#define MODULE_NAME "icm42688p"



static uint8_t read8(SpiDev *self, icm42688p_reg_t addr) {
	uint8_t txbuf = addr | 0x80;
	self->vmt->select(self);
	self->vmt->send(self, &txbuf, 1);
	uint8_t rxbuf;
	self->vmt->receive(self, &rxbuf, 1);
	self->vmt->deselect(self);
	return rxbuf;
}


static void readn(SpiDev *self, uint8_t *data, icm42688p_reg_t addr, size_t len) {
	uint8_t txbuf = addr | 0x80;
	self->vmt->select(self);
	self->vmt->send(self, &txbuf, 1);
	self->vmt->receive(self, data, len);
	self->vmt->deselect(self);
}


static void write8(SpiDev *self, icm42688p_reg_t addr, uint8_t value) {
	uint8_t txbuf[2] = {addr & ~0x80, value};
	self->vmt->select(self);
	self->vmt->send(self, txbuf, 2);
	self->vmt->deselect(self);
}


icm42688p_ret_t icm42688p_detect(Icm42688p *self) {
	write8(self->spidev, ICM42688P_REG_SEL_BANK, 0);
	/* Try to reset the device by writing a (hopefully) non-destructive single bit. */
	write8(self->spidev, ICM42688P_REG_DEVICE_CONFIG, 0x01);
	vTaskDelay(100);

	self->who_am_i = read8(self->spidev, ICM42688P_REG_WHO_AM_I);
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("register WHO_AM_I = %02x"), self->who_am_i);
	if (self->who_am_i == 0x47) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("ICM-42688-P detected"));
		return ICM42688P_RET_OK;
	} else if (self->who_am_i == 0x6f) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("IIM-42652 detected"));
		return ICM42688P_RET_OK;
	}
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("detection failed"));
	return ICM42688P_RET_FAILED;
}


uint16_t icm42688p_fifo_count(Icm42688p *self) {
	uint16_t count = read8(self->spidev, ICM42688P_REG_FIFO_COUNTH);
	count |= read8(self->spidev, ICM42688P_REG_FIFO_COUNTL) << 8;
	return count;
}


waveform_source_ret_t icm42688p_start(Icm42688p *self) {
	write8(self->spidev, ICM42688P_REG_SEL_BANK, 0);
	/* Enable accelerometer low noise mode. */
	write8(self->spidev, ICM42688P_REG_PWR_MGMT0, 0x23);
	vTaskDelay(50);
}


waveform_source_ret_t icm42688p_stop(Icm42688p *self) {
	write8(self->spidev, ICM42688P_REG_SEL_BANK, 0);
	/* Disable everything including the temperature sensor. */
	write8(self->spidev, ICM42688P_REG_PWR_MGMT0, 0x20);
	vTaskDelay(50);
}


waveform_source_ret_t icm42688p_read(Icm42688p *self, void *data, size_t sample_count, size_t *read) {
	write8(self->spidev, ICM42688P_REG_SEL_BANK, 0);
	int16_t *data16 = (int16_t *)data;
	*read = 0;
	int32_t data32[8] = {0};

	uint32_t oversample_count = 8;
	uint8_t oversample_bits = 4;

	/* Avoid reading of an empty FIFO. It gives unpredictable results. */
	uint16_t fifo_count = icm42688p_fifo_count(self);
	if ((sample_count * oversample_count) > fifo_count) {
		sample_count = fifo_count / oversample_count;
	}

	while (sample_count--) {
		memset(data32, 0, sizeof(data32));
		for (size_t ovs = 0; ovs < oversample_count; ovs++) {
			uint8_t header = read8(self->spidev, ICM42688P_REG_FIFO_DATA);
			//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("header = 0x%02x"), header);

			if (header & 0x80) {
				/* FIFO is empty. */
				u_log(system_log, LOG_TYPE_DEBUG, "som narazil");
				return WAVEFORM_SOURCE_RET_OK;
			}
			if (header & 0x40) {
				/* Read accelerometer data. */
				int16_t buf[3] = {0};
				readn(self->spidev, (uint8_t *)&buf, ICM42688P_REG_FIFO_DATA, sizeof(int16_t) * 3);
				data32[0] += buf[0];
				data32[1] += buf[1];
				data32[2] += buf[2];
			}
			if (header & 0x20) {
				/* Read gyroscope data. */
				int16_t buf[3] = {0};
				readn(self->spidev, (uint8_t *)&buf, ICM42688P_REG_FIFO_DATA, sizeof(int16_t) * 3);
				data32[3] += buf[3];
				data32[4] += buf[4];
				data32[5] += buf[5];
			} else {
			}
			if (header & 0x10) {
				/* Temperature (16 bit for 20 bit extended record). */
				int16_t buf = 0;
				readn(self->spidev, (uint8_t *)&buf, ICM42688P_REG_FIFO_DATA, sizeof(int16_t));
				data32[6] += buf;
			} else {
				/* Temperature (8 bit). */
				data32[6] += read8(self->spidev, ICM42688P_REG_FIFO_DATA);
			}
			//if (header & 0x08) {
				/* Read timestamp value. */
				int16_t buf = 0;
				readn(self->spidev, (uint8_t *)&buf, ICM42688P_REG_FIFO_DATA, sizeof(int16_t));
				/** @todo Fix the timestamp */
				data32[7] += buf;
				//data16[7] = 0;
			//}
			//data16[7] = header;

			if (header & 0x10) {
				/* Read the 3 byte extension. */
				uint8_t ext[3] = {0};
				readn(self->spidev, ext, ICM42688P_REG_FIFO_DATA, sizeof(ext));

				data32[0] = (data32[0] * 4) | (ext[0] >> 6);
				data32[1] = (data32[1] * 4) | (ext[1] >> 6);
				data32[2] = (data32[2] * 4) | (ext[2] >> 6);
			}
		}
		for (size_t i = 0; i < 8; i++) {
			data16[i] = data32[i] >> oversample_bits;
		}
		data16 += 8;
		(*read)++;
	}

	return WAVEFORM_SOURCE_RET_OK;
}


waveform_source_ret_t icm42688p_get_format(void *parent, enum waveform_source_format *format, uint32_t *channels) {
	*format = WAVEFORM_SOURCE_FORMAT_S16;
	*channels = 8;
	return WAVEFORM_SOURCE_RET_OK;
}


waveform_source_ret_t icm42688p_set_sample_rate(void *parent, float sample_rate_Hz) {


}


waveform_source_ret_t icm42688p_get_sample_rate(void *parent, float *sample_rate_Hz) {


}


icm42688p_ret_t icm42688p_init_defaults(Icm42688p *self) {
	write8(self->spidev, ICM42688P_REG_SEL_BANK, 0);
	write8(self->spidev, ICM42688P_REG_INT_CONFIG, 0x00);

	/* FIFO size reported in records, data in little endian. */
	write8(self->spidev, ICM42688P_REG_INTF_CONFIG0, 0x40);

	write8(self->spidev, ICM42688P_REG_FIFO_CONFIG, 0x80);
	write8(self->spidev, ICM42688P_REG_GYRO_CONFIG0, 0xc8);
	/* 25 Hz ODR */
	write8(self->spidev, ICM42688P_REG_ACCEL_CONFIG0, 0x67);
	write8(self->spidev, ICM42688P_REG_GYRO_ACCEL_CONFIG0, 0x71);

	/* Enable partial read, include accel, gyro and FSYNC data in the FIFO. */
	write8(self->spidev, ICM42688P_REG_FIFO_CONFIG1, 0x4f);
	/* Do not tag any data, measure from the FSYNC rising edge. */
	write8(self->spidev, ICM42688P_REG_FSYNC_CONFIG, 0x00);

	/* 16 us resolution, enable TMST + delta */
	write8(self->spidev, ICM42688P_REG_TMST_CONFIG, 0x28 | 0x06);
	vTaskDelay(2);

	write8(self->spidev, ICM42688P_REG_SEL_BANK, 1);
	/* Enable FSYNC pin 9 function */
	write8(self->spidev, ICM42688P_REG_INTF_CONFIG5, 0x02);

	write8(self->spidev, ICM42688P_REG_ACCEL_UI_FILT_ORD, 0x15);

	return ICM42688P_RET_OK;
}


icm42688p_ret_t icm42688p_init(Icm42688p *self, SpiDev *spidev) {
	memset(self, 0, sizeof(Icm42688p));
	self->spidev = spidev;

	if (icm42688p_detect(self) != ICM42688P_RET_OK) {
		return ICM42688P_RET_FAILED;
	}

	waveform_source_init(&self->source);
	self->source.parent = self;
	self->source.start = (typeof(self->source.start))icm42688p_start;
	self->source.stop= (typeof(self->source.stop))icm42688p_stop;
	self->source.read = (typeof(self->source.read))icm42688p_read;
	self->source.get_format = (typeof(self->source.get_format))icm42688p_get_format;
	self->source.get_sample_rate = (typeof(self->source.get_sample_rate))icm42688p_get_sample_rate;
	self->source.set_sample_rate = (typeof(self->source.set_sample_rate))icm42688p_set_sample_rate;

	icm42688p_init_defaults(self);

	return ICM42688P_RET_OK;
}


icm42688p_ret_t icm42688p_free(Icm42688p *self) {
	return ICM42688P_RET_OK;
}
