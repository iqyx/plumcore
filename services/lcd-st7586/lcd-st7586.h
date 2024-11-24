/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ST7586 LCD driver service
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <main.h>
#include <interfaces/spi.h>

typedef enum  {
	LCD_ST7586_RET_OK = 0,
	LCD_ST7586_RET_FAILED,
} lcd_st7586_ret_t;


typedef struct lcs_st7586 {
	uint32_t reset_port;
	uint32_t reset_pin;
	uint32_t cd_port;
	uint32_t cd_pin;

	SpiDev *spi;

} LcdSt7586;


lcd_st7586_ret_t lcd_st7586_init(LcdSt7586 *self, SpiDev *spi, uint32_t reset_port, uint32_t reset_pin, uint32_t cd_port, uint32_t cd_pin);
lcd_st7586_ret_t lcd_st7586_free(LcdSt7586 *self);

