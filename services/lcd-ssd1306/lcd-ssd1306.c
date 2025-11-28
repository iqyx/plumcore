/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SSD1306 LCD driver service
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <main.h>
#include <interfaces/i2c-bus.h>

#include "lcd-ssd1306.h"

#define MODULE_NAME "lcd-ssd1306"


static lcd_ssd1306_ret_t lcd_send_cmd(LcdSsd1306 *self, uint8_t cmd) {
	uint8_t txdata[2] = {SSD1306_COMMAND, cmd};
	if (self->i2c->transfer(self->i2c->parent, 0x3c, txdata, sizeof(txdata), NULL, 0) != I2C_BUS_RET_OK) {
		return LCD_SSD1306_RET_FAILED;
	}

	return LCD_SSD1306_RET_OK;
}


static lcd_ssd1306_ret_t lcd_send_cmd_args(LcdSsd1306 *self, uint8_t cmd, size_t nargs, ...) {
	lcd_send_cmd(self, cmd);

	va_list args;
	va_start(args, nargs);
	while (nargs--) {
		uint8_t arg = va_arg(args, uint32_t);
		lcd_send_cmd(self, arg);
	}
	va_end(args);

	return LCD_SSD1306_RET_OK;
}


static lcd_ssd1306_ret_t lcd_send_data(LcdSsd1306 *self) {

	lcd_send_cmd_args(self, SSD1306_SET_COLUMN_ADDR, 2, 0, 127);
	lcd_send_cmd_args(self, SSD1306_SET_PAGE_ADDR, 2, 0, 7);

	for (size_t x = 0; x < 128; x++) {
		uint8_t buf[9] = {0};
		buf[0] = SSD1306_DATA_STREAM;
		for (size_t y = 0; y < 64; y++) {
			if (self->dmem[y * 16 + x / 8] & (0x80 >> (x % 8))) {
				buf[1 + y / 8] |= (0x01 << (y % 8));
			}
		}
		if (self->i2c->transfer(self->i2c->parent, 0x3c, buf, 9, NULL, 0) != I2C_BUS_RET_OK) {
			return LCD_SSD1306_RET_FAILED;
		}
	}

	return LCD_SSD1306_RET_OK;
}


static lcd_ssd1306_ret_t lcd_init_controller(LcdSsd1306 *self) {
	lcd_send_cmd_args(self, SSD1306_DISPLAY_OFF, 0);
	lcd_send_cmd_args(self, SSD1306_SET_OSC_FREQ, 1, 0x80);
	lcd_send_cmd_args(self, SSD1306_SET_MUX_RATIO, 1, 0x3f);
	lcd_send_cmd_args(self, SSD1306_DISPLAY_OFFSET, 1, 0x00);
	lcd_send_cmd_args(self, SSD1306_SET_START_LINE, 0);
	lcd_send_cmd_args(self, SSD1306_DIS_NORMAL, 0);
	lcd_send_cmd_args(self, SSD1306_DIS_ENT_DISP_ON, 0);
	lcd_send_cmd_args(self, SSD1306_SEG_REMAP_OP, 0);
	lcd_send_cmd_args(self, SSD1306_COM_SCAN_DIR_OP, 0);
	lcd_send_cmd_args(self, SSD1306_COM_PIN_CONF, 1, 0x12);
	lcd_send_cmd_args(self, SSD1306_SET_CONTRAST, 1, 0x7f);
	lcd_send_cmd_args(self, SSD1306_SET_PRECHARGE, 1, 0xc2);
	lcd_send_cmd_args(self, SSD1306_VCOM_DESELECT, 1, 0x40);

	lcd_send_cmd_args(self, SSD1306_MEMORY_ADDR_MODE, 1, 0x01);
	lcd_send_cmd_args(self, SSD1306_DEACT_SCROLL, 0);

	lcd_send_cmd_args(self, SSD1306_SET_CHAR_REG, 1, 0x14);
	lcd_send_cmd_args(self, SSD1306_DISPLAY_ON, 0);

	return LCD_SSD1306_RET_OK;
}


/***************************************************************************************************
 * Framebuffer interface API
 ***************************************************************************************************/


static fb_ret_t lcd_ssd1306_fb_write(Fb *self, size_t seek, const void *buf, size_t len, enum fb_mode mode) {
	LcdSsd1306 *lcd = self->parent;

	/* No support for framebuffer conversion yet. */
	if (mode != FB_MODE_G1) {
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


static fb_ret_t lcd_ssd1306_fb_read(Fb *self, size_t seek, void *buf, size_t len, enum fb_mode mode) {
	LcdSsd1306 *lcd = self->parent;

	/* No support for framebuffer conversion yet. */
	if (mode != FB_MODE_G1) {
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


static fb_ret_t lcd_ssd1306_fb_flush(Fb *self) {
	LcdSsd1306 *lcd = self->parent;
	if (lcd_send_data(lcd) != LCD_SSD1306_RET_OK) {
		return FB_RET_FAILED;
	}

	return FB_RET_OK;
}


static fb_ret_t lcd_ssd1306_fb_stat(Fb *self, struct fb_stat *stat) {
	LcdSsd1306 *lcd = self->parent;

	if (stat == NULL) {
		return FB_RET_FAILED;
	}

	stat->mode = FB_MODE_G1;
	stat->w = 128;
	stat->h = 64;

	return FB_RET_OK;
}


static const struct fb_vmt lcd_ssd1306_fb_vmt = {
	.write = lcd_ssd1306_fb_write,
	.read = lcd_ssd1306_fb_read,
	.flush = lcd_ssd1306_fb_flush,
	.stat = lcd_ssd1306_fb_stat,
};


lcd_ssd1306_ret_t lcd_ssd1306_init(LcdSsd1306 *self, I2cBus *i2c) {
	memset(self, 0, sizeof(LcdSsd1306));

	self->i2c = i2c;

	lcd_init_controller(self);

	/** @todo make the framebuffer size configurable, there are 128x32 displays too */
	self->dmem_size = 128 * 64 / 8;
	self->dmem = calloc(self->dmem_size, sizeof(uint8_t));
	if (self->dmem == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate framebuffer memory"));
		return LCD_SSD1306_RET_FAILED;
	}
	lcd_send_data(self);

	self->fb.parent = self;
	self->fb.vmt = &lcd_ssd1306_fb_vmt;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return LCD_SSD1306_RET_OK;
}


lcd_ssd1306_ret_t lcd_ssd1306_free(LcdSsd1306 *self) {
	(void)self;
	return LCD_SSD1306_RET_OK;
}



