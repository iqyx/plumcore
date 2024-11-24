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

#include <main.h>
#include <interfaces/spi.h>

#include "lcd-st7586.h"

#define MODULE_NAME "lcd-st7586"


/***************************************************************************************************
 * Framebuffer interface API
 ***************************************************************************************************/


//static const struct power_vmt generic_power_vmt = {
	//.enable = generic_power_enable,
	//.set_voltage = generic_power_set_voltage,
//};


/*
static void lcd_send_spi(uint8_t data) {
	spi_send8(SPI1, data);
	spi_read8(SPI1);
}


static void lcd_write(uint8_t v) {
	gpio_clear(LCD_CS_PORT, LCD_CS_PIN);
	lcd_send_spi(v);
	gpio_set(LCD_CS_PORT, LCD_CS_PIN);
}


static void lcd_write_buf(uint8_t *buf, size_t len) {
	gpio_clear(LCD_CS_PORT, LCD_CS_PIN);
	while (len) {
		lcd_send_spi(*buf);
		buf++;
		len--;
	}
	gpio_set(LCD_CS_PORT, LCD_CS_PIN);
}
*/

static lcd_st7586_ret_t lcd_reset(LcdSt7586 *self) {
	gpio_set(self->reset_port, self->reset_pin);
	vTaskDelay(10);
	gpio_clear(self->reset_port, self->reset_pin);
	vTaskDelay(10);
	gpio_set(self->reset_port, self->reset_pin);
	vTaskDelay(10);
}


static lcd_st7586_ret_t lcd_send_command(LcdSt7586 *self, uint8_t reg) {
	gpio_clear(self->cd_port, self->cd_pin);

	self->spi->vmt->select(self->spi);
	self->spi->vmt->send(self->spi, &reg, sizeof(reg));
	self->spi->vmt->deselect(self->spi);
}


static lcd_st7586_ret_t lcd_send_data(LcdSt7586 *self, uint8_t reg) {
	gpio_set(self->cd_port, self->cd_pin);

	self->spi->vmt->select(self->spi);
	self->spi->vmt->send(self->spi, &reg, sizeof(reg));
	self->spi->vmt->deselect(self->spi);
}


static lcd_st7586_ret_t lcd_send_data_buf(LcdSt7586 *self, const uint8_t *buf, size_t len) {
	gpio_set(self->cd_port, self->cd_pin);

	self->spi->vmt->select(self->spi);
	self->spi->vmt->send(self->spi, buf, len);
	self->spi->vmt->deselect(self->spi);
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
//      display_white(); // Clear whole DDRAM by ¡°0¡± (384 x 160 x 2)

        lcd_send_command(self, 0x29); // Display ON
}

/*
static void lcd_black(void)  {
	uint8_t data[121];
	for (size_t i = 0; i < 120; i++) {
		data[i] = 0xff;
	}

	lcd_send_command(0x2c);
	for(uint32_t i = 0; i < 170; i++) {
		lcd_send_data_buf(data, 121);
	}
}
*/

static void lcd_pattern(LcdSt7586 *self)  {
	uint8_t data[128] = {0};

	lcd_send_command(self, 0x2c);
	for(uint32_t i = 0; i < 160; i++) {
		for (size_t j = 0; j < 128; j++) {
			data[j] = ((i % 4) << 6) | ((i % 4) << 3);
		}
		lcd_send_data_buf(self, data, 128);
	}
}


lcd_st7586_ret_t lcd_st7586_init(LcdSt7586 *self, SpiDev *spi, uint32_t reset_port, uint32_t reset_pin, uint32_t cd_port, uint32_t cd_pin) {
	memset(self, 0, sizeof(LcdSt7586));

	//self->power.parent = self;
	//self->power.vmt = &generic_power_vmt;
	self->spi = spi;
	self->reset_port = reset_port;
	self->reset_pin = reset_pin;
	self->cd_port = cd_port;
	self->cd_pin = cd_pin;

	lcd_init_controller(self);
	lcd_pattern(self);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized"));
	return LCD_ST7586_RET_OK;
}


lcd_st7586_ret_t lcd_st7586_free(LcdSt7586 *self) {
	(void)self;
	return LCD_ST7586_RET_OK;
}



