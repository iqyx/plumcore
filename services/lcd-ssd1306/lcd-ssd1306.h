/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SSD1306 LCD driver service
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/fb.h>

typedef enum  {
	LCD_SSD1306_RET_OK = 0,
	LCD_SSD1306_RET_FAILED,
} lcd_ssd1306_ret_t;

enum lcd_ssd1306_cmd {
	SSD1306_COMMAND = 0x80,
	SSD1306_COMMAND_STREAM = 0x00,
	SSD1306_DATA = 0xc0,
	SSD1306_DATA_STREAM = 0x40,
	SSD1306_SET_MUX_RATIO = 0xa8,
	SSD1306_DISPLAY_OFFSET = 0xd3,
	SSD1306_DISPLAY_ON = 0xaf,
	SSD1306_DISPLAY_OFF = 0xae,
	SSD1306_DIS_ENT_DISP_ON = 0xa4,
	SSD1306_DIS_IGNORE_RAM = 0xa5,
	SSD1306_DIS_NORMAL = 0xa6,
	SSD1306_DIS_INVERSE = 0xa7,
	SSD1306_DEACT_SCROLL = 0x2e,
	SSD1306_ACTIVE_SCROLL = 0x2f,
	SSD1306_SET_START_LINE = 0x40,
	SSD1306_MEMORY_ADDR_MODE = 0x20,
	SSD1306_SET_COLUMN_ADDR = 0x21,
	SSD1306_SET_PAGE_ADDR = 0x22,
	SSD1306_SEG_REMAP = 0xa0,
	SSD1306_SEG_REMAP_OP = 0xa1,
	SSD1306_COM_SCAN_DIR = 0xc0,
	SSD1306_COM_SCAN_DIR_OP = 0xc8,
	SSD1306_COM_PIN_CONF = 0xda,
	SSD1306_SET_CONTRAST = 0x81,
	SSD1306_SET_OSC_FREQ = 0xd5,
	SSD1306_SET_CHAR_REG = 0x8d,
	SSD1306_SET_PRECHARGE = 0xd9,
	SSD1306_VCOM_DESELECT = 0xdb,
	SSD1306_NOP= 0xe3,
};

typedef struct lcd_ssd1306 {
	I2cBus *i2c;

	Fb fb;

	uint8_t *dmem;
	size_t dmem_size;

} LcdSsd1306;


lcd_ssd1306_ret_t lcd_ssd1306_init(LcdSsd1306 *self, I2cBus *i2c);
lcd_ssd1306_ret_t lcd_ssd1306_free(LcdSsd1306 *self);

