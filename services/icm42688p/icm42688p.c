#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"

#include "interface_spidev.h"
#include "icm42688p.h"
#include "u_log.h"
#include "u_assert.h"
#include <waveform_source.h>

#define MODULE_NAME "icm42688p"



static uint8_t read8(struct interface_spidev *spidev, icm42688p_reg_t addr) {
	uint8_t txbuf = addr | 0x80;
	interface_spidev_select(spidev);
	interface_spidev_send(spidev, &txbuf, 1);
	uint8_t rxbuf;
	interface_spidev_receive(spidev, &rxbuf, 1);
	interface_spidev_deselect(spidev);
	return rxbuf;
}


static void readn(struct interface_spidev *spidev, uint8_t *data, icm42688p_reg_t addr, size_t len) {
	uint8_t txbuf = addr | 0x80;
	interface_spidev_select(spidev);
	interface_spidev_send(spidev, &txbuf, 1);
	interface_spidev_receive(spidev, data, len);
	interface_spidev_deselect(spidev);
}


static void write8(struct interface_spidev *spidev, icm42688p_reg_t addr, uint8_t value) {
	uint8_t txbuf[2] = {addr & ~0x80, value};
	interface_spidev_select(spidev);
	interface_spidev_send(spidev, txbuf, 2);
	interface_spidev_deselect(spidev);
}


icm42688p_ret_t icm42688p_detect(Icm42688p *self) {
	write8(self->spidev, ICM42688P_REG_SEL_BANK, 0);
	/* Try to reset the device by writing a (hopefully) non-destructive single bit. */
	write8(self->spidev, ICM42688P_REG_DEVICE_CONFIG, 0x01);
	vTaskDelay(10);

	self->who_am_i = read8(self->spidev, ICM42688P_REG_WHO_AM_I);
	if (self->who_am_i == 0x47) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("detect ok, who_am_1 = %02x"), self->who_am_i);
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
	write8(self->spidev, ICM42688P_REG_PWR_MGMT0, 0x03);
	vTaskDelay(50);
}


waveform_source_ret_t icm42688p_stop(Icm42688p *self) {
	write8(self->spidev, ICM42688P_REG_SEL_BANK, 0);
	write8(self->spidev, ICM42688P_REG_PWR_MGMT0, 0x00);
	vTaskDelay(50);
}


waveform_source_ret_t icm42688p_read(Icm42688p *self, void *data, size_t sample_count, size_t *read) {
	write8(self->spidev, ICM42688P_REG_SEL_BANK, 0);
	int16_t *data16 = (int16_t *)data;
	*read = 0;
	while (sample_count--) {
		uint8_t header = read8(self->spidev, ICM42688P_REG_FIFO_DATA);
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("header = 0x%02x"), header);

		if (header & 0x80) {
			/* FIFO is empty. */
			return WAVEFORM_SOURCE_RET_OK;
		}
		if (header & 0x40) {
			/* Read accelerometer data. */
			readn(self->spidev, (uint8_t *)&(data16[0]), ICM42688P_REG_FIFO_DATA, sizeof(int16_t) * 3);
		}
		if (header & 0x20) {
			/* Read gyroscope data. */
			readn(self->spidev, (uint8_t *)&(data16[3]), ICM42688P_REG_FIFO_DATA, sizeof(int16_t) * 3);
		}
		/* Temperature (8 bit). */
		data16[6] = read8(self->spidev, ICM42688P_REG_FIFO_DATA);
		if (header & 0x08) {
			/* Read timestamp value. */
			readn(self->spidev, (uint8_t *)&(data16[7]), ICM42688P_REG_FIFO_DATA, sizeof(int16_t));
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
	write8(self->spidev, ICM42688P_REG_ACCEL_CONFIG0, 0x68);
	write8(self->spidev, ICM42688P_REG_GYRO_ACCEL_CONFIG0, 0x71);

	/* Enable partial read, include accel, gyro and FSYNC data in the FIFO. */
	write8(self->spidev, ICM42688P_REG_FIFO_CONFIG1, 0x4f);
	/* Do not tag any data, measure from the FSYNC rising edge. */
	write8(self->spidev, ICM42688P_REG_FSYNC_CONFIG, 0x00);

	/* 16 us resolution, enable TMST registers */
	write8(self->spidev, ICM42688P_REG_TMST_CONFIG, 0x36);
	vTaskDelay(2);

	write8(self->spidev, ICM42688P_REG_SEL_BANK, 1);
	/* Enable FSYNC pin 9 function */
	write8(self->spidev, ICM42688P_REG_INTF_CONFIG5, 0x02);
	
	return ICM42688P_RET_OK;
}


icm42688p_ret_t icm42688p_init(Icm42688p *self, struct interface_spidev *spidev) {
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
