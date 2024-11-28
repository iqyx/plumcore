/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ST7586 LCD driver service
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/** @todo remove libopencm3 dependency */
#include <libopencm3/stm32/gpio.h>

#include <main.h>
#include <interfaces/spi.h>

#include "lcd-st7586.h"

#define MODULE_NAME "lcd-st7586"


static lcd_st7586_ret_t lcd_reset(LcdSt7586 *self) {
	gpio_set(self->reset_port, self->reset_pin);
	vTaskDelay(10);
	gpio_clear(self->reset_port, self->reset_pin);
	vTaskDelay(10);
	gpio_set(self->reset_port, self->reset_pin);
	vTaskDelay(10);

	return LCD_ST7586_RET_OK;
}


static lcd_st7586_ret_t lcd_send_command(LcdSt7586 *self, uint8_t reg) {
	gpio_clear(self->cd_port, self->cd_pin);

	self->spi->vmt->select(self->spi);
	self->spi->vmt->send(self->spi, &reg, sizeof(reg));
	self->spi->vmt->deselect(self->spi);

	return LCD_ST7586_RET_OK;
}


static lcd_st7586_ret_t lcd_send_data(LcdSt7586 *self, uint8_t reg) {
	gpio_set(self->cd_port, self->cd_pin);

	self->spi->vmt->select(self->spi);
	self->spi->vmt->send(self->spi, &reg, sizeof(reg));
	self->spi->vmt->deselect(self->spi);

	return LCD_ST7586_RET_OK;
}


static lcd_st7586_ret_t lcd_send_data_buf(LcdSt7586 *self, const uint8_t *buf, size_t len) {
	gpio_set(self->cd_port, self->cd_pin);

	self->spi->vmt->select(self->spi);
	self->spi->vmt->send(self->spi, buf, len);
	self->spi->vmt->deselect(self->spi);

	return LCD_ST7586_RET_OK;
}


static lcd_st7586_ret_t lcd_init_controller(LcdSt7586 *self) {
	lcd_reset(self);

        lcd_send_command(self, 0x11); // Sleep Out
        lcd_send_command(self, 0x28); // Display OFF
        vTaskDelay(10);
        lcd_send_command(self, 0xC0); // Vop = B9h
        lcd_send_data(self, 0x40);
        lcd_send_data(self, 0x01);
        lcd_send_command(self, 0xC3); // BIAS = 1/14
        lcd_send_data(self, 0x00);
        lcd_send_command(self, 0xC4); // Booster = x8
        lcd_send_data(self, 0x07);
        lcd_send_command(self, 0xD0); // Enable Analog Circuit
        lcd_send_data(self, 0x1D);
        lcd_send_command(self, 0xB5); // N-Line = 0
        lcd_send_data(self, 0x00);
        lcd_send_command(self, 0x38); // Grayscale mode
        lcd_send_command(self, 0x3A); // Enable DDRAM Interface
        lcd_send_data(self, 0x02);
        lcd_send_command(self, 0x36); // Scan Direction Setting
        lcd_send_data(self, 0xc0);   //COM:C160--C1   SEG: SEG384-SEG1
        lcd_send_command(self, 0xB0); // Duty Setting
        lcd_send_data(self, 0x9F);

        lcd_send_command(self, 0x20); // Display Inversion OFF
        lcd_send_command(self, 0x2A); // Column Address Setting
        lcd_send_data(self, 0x00); // SEG0 -> SEG384
        lcd_send_data(self, 0x00);
        lcd_send_data(self, 0x00);
        lcd_send_data(self, 0x7F);
        lcd_send_command(self, 0x2B); // Row Address Setting
        lcd_send_data(self, 0x00); // COM0 -> COM160
        lcd_send_data(self, 0x00);
        lcd_send_data(self, 0x00);
        lcd_send_command(self, 0x9F);
        lcd_send_command(self, 0x29); // Display ON

	return LCD_ST7586_RET_OK;
}


static lcd_st7586_ret_t lcd_clear(LcdSt7586 *self)  {
	uint8_t data[128] = {0};

	lcd_send_command(self, 0x2c);
	for(uint32_t i = 0; i < 160; i++) {
		lcd_send_data_buf(self, data, 128);
	}

	return LCD_ST7586_RET_OK;
}


/***************************************************************************************************
 * Framebuffer interface API
 ***************************************************************************************************/


static fb_ret_t lcd_st7586_fb_write(Fb *self, size_t seek, const void *buf, size_t len, enum fb_mode mode) {
	LcdSt7586 *lcd = self->parent;

	/* No support for framebuffer conversion yet. */
	if (mode != FB_MODE_G2) {
		return FB_RET_FAILED;
	}
	if (seek >= lcd->dmem_size) {
		return FB_RET_FAILED;
	}
	if ((seek + len) > lcd->dmem_size) {
		return FB_RET_FAILED;
	}
	memcpy(lcd->dmem + seek, buf, len);

	return FB_RET_OK;
}


static fb_ret_t lcd_st7586_fb_read(Fb *self, size_t seek, void *buf, size_t len, enum fb_mode mode) {
	LcdSt7586 *lcd = self->parent;

	/* No support for framebuffer conversion yet. */
	if (mode != FB_MODE_G2) {
		return FB_RET_FAILED;
	}
	if (seek >= lcd->dmem_size) {
		return FB_RET_FAILED;
	}
	if ((seek + len) > lcd->dmem_size) {
		return FB_RET_FAILED;
	}
	memcpy(buf, lcd->dmem + seek, len);

	return FB_RET_OK;
}


static fb_ret_t lcd_st7586_fb_flush(Fb *self) {
	LcdSt7586 *lcd = self->parent;
	uint8_t *buf = lcd->dmem;

	uint8_t data[128] = {0};
	lcd_send_command(lcd, 0x2c);
	for(uint32_t i = 0; i < 160; i++) {
		for (size_t j = 0; j < 60 ; j++) {
			uint8_t p = *buf;
			data[8 + j * 2] = (p & 0xc0) >> 3 | (p & 0x30) << 2;
			data[8 + j * 2 + 1] = (p & 0x0c) << 1 | (p & 0x03) << 6;
			buf++;
		}
		lcd_send_data_buf(lcd, data, 128);
	}

	return FB_RET_OK;
}


static const struct fb_vmt lcd_st7586_fb_vmt = {
	.write = lcd_st7586_fb_write,
	.read = lcd_st7586_fb_read,
	.flush = lcd_st7586_fb_flush,
};


lcd_st7586_ret_t lcd_st7586_init(LcdSt7586 *self, SpiDev *spi, uint32_t reset_port, uint32_t reset_pin, uint32_t cd_port, uint32_t cd_pin) {
	memset(self, 0, sizeof(LcdSt7586));

	self->spi = spi;
	self->reset_port = reset_port;
	self->reset_pin = reset_pin;
	self->cd_port = cd_port;
	self->cd_pin = cd_pin;

	lcd_init_controller(self);
	lcd_clear(self);

	/* Initialize the framebuffer memory */
	self->dmem = calloc(240 * 160 / 4, sizeof(uint8_t));
	if (self->dmem == NULL) {
		return LCD_ST7586_RET_FAILED;
	}
	self->dmem_size = 240 * 160 / 4;

	self->fb.parent = self;
	self->fb.vmt = &lcd_st7586_fb_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return LCD_ST7586_RET_OK;
}


lcd_st7586_ret_t lcd_st7586_free(LcdSt7586 *self) {
	(void)self;
	return LCD_ST7586_RET_OK;
}



