/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ST7586 LCD driver service
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/gpio.h>
#include <interfaces/spi.h>
#include <interfaces/fb.h>

typedef enum  {
	LCD_ST7586_RET_OK = 0,
	LCD_ST7586_RET_FAILED,
} lcd_st7586_ret_t;


typedef struct lcs_st7586 {
	Gpio *reset;
	Gpio *cd;

	SpiDev *spi;
	Fb fb;

	uint8_t *dmem;
	size_t dmem_size;

} LcdSt7586;


lcd_st7586_ret_t lcd_st7586_init(LcdSt7586 *self, SpiDev *spi, Gpio *reset, Gpio *cd);
lcd_st7586_ret_t lcd_st7586_free(LcdSt7586 *self);
lcd_st7586_ret_t lcd_st7586_set_contrast(LcdSt7586 *self, float contrast);

