/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 basic port (STM32U575VIT6)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <stm32u5xx.h>

#include <main.h>
#include "port.h"

#include <interfaces/servicelocator.h>

/* Low level drivers for the STM32U5 family */
#include <services/stm32-gpio/stm32-gpio.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-octospi-flash/stm32-octospi-flash.h>
#include <services/stm32-i2c/stm32-i2c.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/lcd-st7586/lcd-st7586.h>
#include <services/ncn26010/ncn26010.h>
#include <services/st67w611/st67w611.h>
#include <services/lp581x-led/lp581x-led.h>
#include <services/lp586x-led/lp586x-led.h>
#include <services/bq25798/bq25798.h>
#include <services/bq27441/bq27441.h>
#include <services/gpio-led/gpio-led.h>
#include <services/stm32-timer/stm32-timer.h>
#include <services/pwm-beeper/pwm-beeper.h>
#include <services/pcal6408a-gpio/pcal6408a-gpio.h>
#include <services/gpio-keypad/gpio-keypad.h>
#include <services/keypad-layout/keypad-layout.h>
#include <interfaces/led-sequences.h>
#include <interfaces/beeper-sequences.h>
#include <interfaces/event.h>
#include <interfaces/applet.h>
#include <applets/hello-world/hello-world.h>
#include <applets/hello-wren/generated/hello-wren.h>
#include <applets/settings/settings.h>
#include <applets/live-data/live-data.h>


#define MODULE_NAME "port"


/**
 * Port specific global variables and singleton instances.
 */

uint32_t SystemCoreClock;

Stm32Gpio gpioa;
Stm32Gpio gpiob;
Stm32Gpio gpioc;
Stm32Gpio gpiod;
Stm32Gpio gpioe;
Stm32OctospiFlash octospi_flash;


int32_t port_early_init(void) {
	/* Run the system clock straight from the 16 MHz HSE crystal, bypassing the PLL entirely. This is the
	 * simplest possible clock tree: no PLL configuration, no reference dividers, just HSE as SYSCLK. Before
	 * raising the frequency from the 4 MHz MSIS reset clock, add a flash wait state (1 WS covers 16 MHz in
	 * any of the reset voltage scaling ranges). */
	FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_1WS;
	while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_1WS) {
		;
	}

	RCC->CR |= RCC_CR_HSEON;
	while ((RCC->CR & RCC_CR_HSERDY) == 0) {
		;
	}

	RCC->CFGR1 = (RCC->CFGR1 & ~RCC_CFGR1_SW) | RCC_CFGR1_SW_1;
	while ((RCC->CFGR1 & RCC_CFGR1_SWS) != RCC_CFGR1_SWS_1) {
		;
	}

	SystemCoreClock = 16e6;

	/* Enable full access to the FPU (CP10/CP11) before any floating point code runs. */
	SCB->CPACR |= (0xf << 20);

	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOAEN;
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOBEN;
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOCEN;
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIODEN;
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOEEN;

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

Stm32Uart uart4;
static void port_setup_console(void) {
	RCC->APB1ENR1 |= RCC_APB1ENR1_UART4EN;

	/* UART4 TX on PA0, RX on PA1, AF8. */
	gpioa.pin[0].vmt->set_mode(&(gpioa.pin[0]), MODE_ALTERNATE);
	gpioa.pin[0].vmt->set_pinmux(&(gpioa.pin[0]), 8);
	gpioa.pin[1].vmt->set_mode(&(gpioa.pin[1]), MODE_ALTERNATE);
	gpioa.pin[1].vmt->set_pinmux(&(gpioa.pin[1]), 8);

	/* Initialise and configure the UART */
	stm32_uart_init(&uart4, (void *)UART4);
	uart4.uart.vmt->set_bitrate(&uart4.uart, 115200);
	stm32_uart_set_rxtx_swap(&uart4, true);

	NVIC_EnableIRQ(UART4_IRQn);
	NVIC_SetPriority(UART4_IRQn, 7);

	/* Advertise the console stream output and set it as default for log output. */
	Stream *console = &uart4.stream;
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)console, "console");
	u_log_set_stream(console);
}


void uart4_isr(void);
void uart4_isr(void) {
	stm32_uart_interrupt_handler(&uart4);
}


/**********************************************************************************************************************
 * nbus2 backplane stream on USART1
 **********************************************************************************************************************/

/* USART1 carries the nbus2 protocol on the backplane connecting to the measurement card. The port only
 * sets up the USART and advertises its byte stream; the application discovers the stream and builds the
 * nbus2 stack (framing, MAC and the protocols) on top of it. */
Stm32Uart uart1;

Gpio *nbus2_shdn_gpio = &(gpiod.pin[14]);

static void port_setup_nbus2(void) {
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

	/* The nbus2 transceiver shutdown on PD14 is active high; drive it low to enable the transceiver. */
	nbus2_shdn_gpio->vmt->set_mode(nbus2_shdn_gpio, MODE_OUTPUT);
	nbus2_shdn_gpio->vmt->set(nbus2_shdn_gpio, false);

	/* USART1 TX on PA9, RX on PA10, AF7. */
	gpioa.pin[9].vmt->set_mode(&(gpioa.pin[9]), MODE_ALTERNATE);
	gpioa.pin[9].vmt->set_pinmux(&(gpioa.pin[9]), 7);
	gpioa.pin[10].vmt->set_mode(&(gpioa.pin[10]), MODE_ALTERNATE);
	gpioa.pin[10].vmt->set_pinmux(&(gpioa.pin[10]), 7);

	stm32_uart_init(&uart1, (void *)USART1);
	stm32_uart_set_rto(&uart1, true);
	uart1.uart.vmt->set_bitrate(&uart1.uart, 1000000);

	NVIC_EnableIRQ(USART1_IRQn);
	NVIC_SetPriority(USART1_IRQn, 7);

	/* Advertise the raw backplane stream. The application frames nbus2 datagrams onto it. */
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&uart1.stream, "nbus");
}


void usart1_isr(void);
void usart1_isr(void) {
	stm32_uart_interrupt_handler(&uart1);
}


/**********************************************************************************************************************
 * OCTOSPI NOR flash initialisation
 **********************************************************************************************************************/

static void port_setup_flash(void) {
	/* Enable the OCTOSPI1 peripheral and its I/O manager. */
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_OCTOSPIMEN;
	RCC->AHB2ENR2 |= RCC_AHB2ENR2_OCTOSPI1EN;

	/* OCTOSPI1 is wired to the I/O manager port 1 on PE10..PE15, AF10:
	 * CLK on PE10, NCS on PE11 and IO0..IO3 on PE12..PE15. Use the highest
	 * slew rate to keep the QSPI signal integrity. */
	for (int i = 10; i <= 15; i++) {
		gpioe.pin[i].vmt->set_mode(&(gpioe.pin[i]), MODE_ALTERNATE);
		gpioe.pin[i].vmt->set_pinmux(&(gpioe.pin[i]), 10);
		gpioe.pin[i].vmt->set_ospeed(&(gpioe.pin[i]), OSPEED_VERYHIGH);
	}

	/* Route OCTOSPI1 (source 0) to the I/O manager physical port 1. */
	OCTOSPIM->PCR[0] = OCTOSPIM_PCR_CLKEN | OCTOSPIM_PCR_NCSEN | OCTOSPIM_PCR_IOLEN;

	if (stm32_octospi_flash_qspi_init(&octospi_flash, (void *)OCTOSPI1) != STM32_OCTOSPI_FLASH_RET_OK) {
		return;
	}
	stm32_octospi_flash_set_prescaler(&octospi_flash, 1);

	/* Advertise the raw flash interface. */
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)&octospi_flash.iface, "flash1");
}


/**********************************************************************************************************************
 * I2C1 bus
 **********************************************************************************************************************/

Stm32I2c i2c1;

Gpio *i2c1_scl = &(gpiob.pin[8]);
Gpio *i2c1_sda = &(gpiob.pin[7]);

static void port_setup_i2c(void) {
	/* Unstuck any slave that may still be holding the bus from before a firmware reset, while the pins are
	 * still plain GPIOs (not yet muxed to the I2C peripheral). */
	stm32_i2c_recovery(i2c1_sda, i2c1_scl);

	/* I2C1 SCL on PB8, SDA on PB7, AF4, open-drain. */
	i2c1_scl->vmt->set_mode(i2c1_scl, MODE_ALTERNATE);
	i2c1_scl->vmt->set_otype(i2c1_scl, OTYPE_OD);
	i2c1_scl->vmt->set_pinmux(i2c1_scl, 4);

	i2c1_sda->vmt->set_mode(i2c1_sda, MODE_ALTERNATE);
	i2c1_sda->vmt->set_otype(i2c1_sda, OTYPE_OD);
	i2c1_sda->vmt->set_pinmux(i2c1_sda, 4);

	/* Configure and run the I2C driver and peripheral when GPIO is ready. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
	NVIC_EnableIRQ(I2C1_EV_IRQn);
	NVIC_SetPriority(I2C1_EV_IRQn, 7);
	NVIC_EnableIRQ(I2C1_ER_IRQn);
	NVIC_SetPriority(I2C1_ER_IRQn, 7);
	stm32_i2c_init(&i2c1, (void *)I2C1);
	stm32_i2c_set_speed(&i2c1, 100000);

	stm32_i2c_scan(&i2c1);
}


void i2c1_ev_isr(void);
void i2c1_ev_isr(void) {
	stm32_i2c_irq_handler(&i2c1);
}


void i2c1_er_isr(void);
void i2c1_er_isr(void) {
	stm32_i2c_irq_handler(&i2c1);
}


/**********************************************************************************************************************
 * I2C2 bus
 **********************************************************************************************************************/

Stm32I2c i2c2;

Gpio *i2c2_scl = &(gpiob.pin[10]);
Gpio *i2c2_sda = &(gpiob.pin[14]);

static void port_setup_pm_i2c(void) {
	/* Unstuck any slave that may still be holding the bus from before a firmware reset, while the pins are
	 * still plain GPIOs (not yet muxed to the I2C peripheral). */
	stm32_i2c_recovery(i2c2_sda, i2c2_scl);

	/* I2C2 SCL on PB10, SDA on PB14, AF4, open-drain. The board has no external pull-ups on this bus, so the
	 * internal ones are enabled; they are weak, so keep the bus short and the clock slow. */
	i2c2_scl->vmt->set_mode(i2c2_scl, MODE_ALTERNATE);
	i2c2_scl->vmt->set_otype(i2c2_scl, OTYPE_OD);
	i2c2_scl->vmt->set_pull(i2c2_scl, PULL_UP);
	i2c2_scl->vmt->set_pinmux(i2c2_scl, 4);

	i2c2_sda->vmt->set_mode(i2c2_sda, MODE_ALTERNATE);
	i2c2_sda->vmt->set_otype(i2c2_sda, OTYPE_OD);
	i2c2_sda->vmt->set_pull(i2c2_sda, PULL_UP);
	i2c2_sda->vmt->set_pinmux(i2c2_sda, 4);

	/* Configure and run the I2C driver and peripheral when GPIO is ready. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_I2C2EN;
	NVIC_EnableIRQ(I2C2_EV_IRQn);
	NVIC_SetPriority(I2C2_EV_IRQn, 7);
	NVIC_EnableIRQ(I2C2_ER_IRQn);
	NVIC_SetPriority(I2C2_ER_IRQn, 7);
	stm32_i2c_init(&i2c2, (void *)I2C2);
	stm32_i2c_set_speed(&i2c2, 100000);

	//while (true) {
		stm32_i2c_scan(&i2c2);
		//vTaskDelay(100);
	//}
}


void i2c2_ev_isr(void);
void i2c2_ev_isr(void) {
	stm32_i2c_irq_handler(&i2c2);
}


void i2c2_er_isr(void);
void i2c2_er_isr(void) {
	stm32_i2c_irq_handler(&i2c2);
}


/**********************************************************************************************************************
 * Status RGB LEDs driven by a LP5812
 **********************************************************************************************************************/

Lp581x lp5812;
GpioLed led_bat;
GpioLed led_sys;
GpioLed led_mem;
GpioLed led_ble_wifi;

/* Top LEDs driven by a LP5862 LED matrix driver. Its 36 LED dots drive twelve RGB LEDs. */
Lp586x lp5862;
GpioLed top_led[12];

Gpio *top_led_en_gpio = &(gpiod.pin[11]);

/* Wire a single RGB LED to three consecutive LP5812 PWM channels (red, green, blue). */
static void led_setup(GpioLed *led, size_t ch_base) {
	Pwm *r = NULL;
	Pwm *g = NULL;
	Pwm *b = NULL;
	lp581x_get_pwm(&lp5812, ch_base + 0, &r);
	lp581x_get_pwm(&lp5812, ch_base + 1, &g);
	lp581x_get_pwm(&lp5812, ch_base + 2, &b);

	gpio_led_init(led, NULL, NULL, NULL);
	gpio_led_set_pwm(led, r, g, b);
}

/* Wire a single top RGB LED to three consecutive LP5862 PWM channels (red, green, blue). */
static void top_led_setup(GpioLed *led, size_t ch_base) {
	Pwm *r = NULL;
	Pwm *g = NULL;
	Pwm *b = NULL;
	lp586x_get_pwm(&lp5862, ch_base + 0, &r);
	lp586x_get_pwm(&lp5862, ch_base + 1, &g);
	lp586x_get_pwm(&lp5862, ch_base + 2, &b);

	gpio_led_init(led, NULL, NULL, NULL);
	/* Inverted RGB! */
	gpio_led_set_pwm(led, b, g, r);
}

#define LED_SEQ_GREEN_BLINK LED_SEQ { \
	LED_SEQ_SET | LED_SEQ_RGB(0x00, 0xff, 0x22) | LED_SEQ_TIME_MS(256), \
	LED_SEQ_SET | LED_SEQ_OFF | LED_SEQ_TIME_MS(256), \
	LED_SEQ_END \
} \

static void port_setup_leds(void) {
	/* C variant of the LP5812 (chip address 0x16). Its 12 PWM channels drive four RGB LEDs. */
	if (lp581x_init(&lp5812, &i2c1.bus, 0x16, LP581X_TYPE_LP5812) != LP581X_RET_OK) {
		return;
	}

	for (size_t i = 0; i < 12; i++) {
		lp581x_set_max_current(&lp5812, i, 0.5f);
	}

	led_setup(&led_bat, 0);
	led_setup(&led_sys, 3);
	led_setup(&led_mem, 6);
	led_setup(&led_ble_wifi, 9);

	led_sys.led.vmt->set(&led_sys.led, LED_COLOR_RGB(0, 255, 15));

	/* Top LEDs driven by a LP5862. EN on PD11 gates the controller, so power it up before talking to it. */
	top_led_en_gpio->vmt->set_mode(top_led_en_gpio, MODE_OUTPUT);
	top_led_en_gpio->vmt->set(top_led_en_gpio, false);
	vTaskDelay(2);
	top_led_en_gpio->vmt->set(top_led_en_gpio, true);
	vTaskDelay(2);

	/* Both ADDR pins low select chip address 0x10. Its 36 LED dots drive twelve RGB LEDs. */
	const struct lp586x_conf lp5862_conf = {
		.i2c = &i2c1.bus,
		.addr = 0x10,
		.type = LP586X_TYPE_LP5862,
	};
	if (lp586x_init(&lp5862, &lp5862_conf) != LP586X_RET_OK) {
		return;
	}

	for (size_t i = 0; i < 36; i++) {
		lp586x_set_max_current(&lp5862, i, 0.25f);
	}

	for (size_t i = 0; i < 12; i++) {
		top_led_setup(&top_led[i], i * 3);
		top_led[i].led.vmt->set(&(top_led[i].led), LED_COLOR_RGB(15, 0, 15));
	}

	//top_led[1].led.vmt->set(&(top_led[1].led), LED_COLOR_RGB(0, 255, 31));
	//top_led[3].led.vmt->set(&(top_led[3].led), LED_COLOR_RGB(255, 0, 31));



}


/**********************************************************************************************************************
 * LCD backlight driven by a LP5810
 **********************************************************************************************************************/

Lp581x lp5810;

static void port_setup_lcd(void) {
	/* A variant of the LP5810 (chip address 0x14). Its four outputs drive the LCD backlight LEDs, which are
	 * all wired in parallel. */
	if (lp581x_init(&lp5810, &i2c1.bus, 0x14, LP581X_TYPE_LP5810) != LP581X_RET_OK) {
		return;
	}

	/* Run all four parallel channels at a tenth of the full-scale current and turn them fully on. */
	for (size_t i = 0; i < 4; i++) {
		lp581x_set_max_current(&lp5810, i, 1.0f);

		Pwm *pwm = NULL;
		lp581x_get_pwm(&lp5810, i, &pwm);
		pwm->vmt->set_pwm(pwm, 0.25f);
	}
}


/**********************************************************************************************************************
 * ST7586 LCD on SPI2
 **********************************************************************************************************************/

Stm32SpiBus spi2;
Stm32SpiDev spi2_lcd;
LcdSt7586 lcd;

Gpio *spi2_sck  = &(gpiod.pin[1]);
Gpio *spi2_miso = &(gpiod.pin[3]);
Gpio *spi2_mosi = &(gpiod.pin[4]);
Gpio *lcd_reset = &(gpioc.pin[7]);
Gpio *lcd_cs    = &(gpioc.pin[6]);
Gpio *lcd_cd    = &(gpioa.pin[8]);

static void port_setup_display(void) {
	/* SPI2 SCK on PD1, MISO on PD3, MOSI on PD4, AF5. */
	spi2_sck->vmt->set_mode(spi2_sck, MODE_ALTERNATE);
	spi2_sck->vmt->set_pinmux(spi2_sck, 5);
	spi2_sck->vmt->set_ospeed(spi2_sck, OSPEED_VERYHIGH);
	spi2_miso->vmt->set_mode(spi2_miso, MODE_ALTERNATE);
	spi2_miso->vmt->set_pinmux(spi2_miso, 5);
	spi2_miso->vmt->set_ospeed(spi2_miso, OSPEED_VERYHIGH);
	spi2_mosi->vmt->set_mode(spi2_mosi, MODE_ALTERNATE);
	spi2_mosi->vmt->set_pinmux(spi2_mosi, 5);
	spi2_mosi->vmt->set_ospeed(spi2_mosi, OSPEED_VERYHIGH);

	/* Configure and run the SPI bus and the LCD chip-select device. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
	stm32_spibus_init(&spi2, (void *)SPI2, STM32_SPI_PER_TYPE_SPI);
	spi2.bus.vmt->set_sck_freq(&spi2.bus, 16e6);
	spi2.bus.vmt->set_mode(&spi2.bus, 0, 0);
	stm32_spidev_init(&spi2_lcd, &spi2.bus, lcd_cs);

	/* LCD reset on PC7, command/data on PA8. */
	lcd_st7586_init(&lcd, &spi2_lcd.dev, lcd_reset, lcd_cd);
	lcd_st7586_set_contrast(&lcd, 0.305f);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FB, (Interface *)&lcd.fb, "lcd");
}


/**********************************************************************************************************************
 * NCN26010 10Base-T1S ethernet on SPI2
 **********************************************************************************************************************/

Stm32SpiDev spi2_t1s;
Ncn26010 t1s;

Gpio *t1s_cs  = &(gpiod.pin[0]);
Gpio *t1s_irq = &(gpiod.pin[15]);

static void port_setup_t1s(void) {
	/* IRQ on PD15 is an open-drain output of the NCN26010, pulled up on the MCU side. */
	t1s_irq->vmt->set_mode(t1s_irq, MODE_INPUT);
	t1s_irq->vmt->set_pull(t1s_irq, PULL_UP);

	/* The NCN26010 shares SPI2 with the LCD, so only a new chip-select device is needed. CS on PD0. */
	stm32_spidev_init(&spi2_t1s, &spi2.bus, t1s_cs);

	ncn26010_init(&t1s, &spi2_t1s.dev);
	ncn26010_sleep(&t1s);
}


/**********************************************************************************************************************
 * BLE/WIFI module power control
 **********************************************************************************************************************/

Stm32SpiBus spi1;
Stm32SpiDev spi1_ble;
St67w611 ble;

Gpio *ble_sck   = &(gpioa.pin[5]);
Gpio *ble_miso  = &(gpioa.pin[6]);
Gpio *ble_mosi  = &(gpioa.pin[7]);
Gpio *ble_cs    = &(gpioa.pin[4]);
Gpio *ble_en_gpio   = &(gpiob.pin[4]);
Gpio *ble_boot_gpio = &(gpiob.pin[6]);
Gpio *ble_rdy_gpio  = &(gpiod.pin[7]);

static void port_setup_ble(void) {
	/* SPI1 SCK on PA5, MISO on PA6, MOSI on PA7, AF5. */
	ble_sck->vmt->set_mode(ble_sck, MODE_ALTERNATE);
	ble_sck->vmt->set_pinmux(ble_sck, 5);
	ble_sck->vmt->set_ospeed(ble_sck, OSPEED_VERYHIGH);
	ble_miso->vmt->set_mode(ble_miso, MODE_ALTERNATE);
	ble_miso->vmt->set_pinmux(ble_miso, 5);
	ble_miso->vmt->set_ospeed(ble_miso, OSPEED_VERYHIGH);
	ble_mosi->vmt->set_mode(ble_mosi, MODE_ALTERNATE);
	ble_mosi->vmt->set_pinmux(ble_mosi, 5);
	ble_mosi->vmt->set_ospeed(ble_mosi, OSPEED_VERYHIGH);

	/* EN on PB4 powers the module (active high), BOOT on PB6 selects the boot source and RDY on PD7
	 * is the module's SPI handshake output. The driver drives EN/BOOT during its probe. RDY is pulled
	 * down so it reads low while the module is still booting and has not yet driven the line. */
	ble_en_gpio->vmt->set_mode(ble_en_gpio, MODE_OUTPUT);
	ble_en_gpio->vmt->set(ble_en_gpio, false);
	ble_boot_gpio->vmt->set_mode(ble_boot_gpio, MODE_OUTPUT);
	ble_boot_gpio->vmt->set(ble_boot_gpio, false);
	ble_rdy_gpio->vmt->set_mode(ble_rdy_gpio, MODE_INPUT);
	ble_rdy_gpio->vmt->set_pull(ble_rdy_gpio, PULL_DOWN);

	/* Configure and run the SPI bus with the module's chip-select device. CS on PA4, mode 0 (CPOL=0,
	 * CPHA=0), MSB first. */
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
	stm32_spibus_init(&spi1, (void *)SPI1, STM32_SPI_PER_TYPE_SPI);
	spi1.bus.vmt->set_sck_freq(&spi1.bus, 2e6);
	spi1.bus.vmt->set_mode(&spi1.bus, 0, 0);
	stm32_spidev_init(&spi1_ble, &spi1.bus, ble_cs);
	/* The ST67W611M1 NCP protocol uses an active-high (inverted) chip-select. */
	stm32_spidev_set_cs_inverted(&spi1_ble, true);

	/* The module is also wired to USART2 (TX on PD5, RX on PD6, AF7), left unconfigured. Per ST, the
	 * module's UART is a boot/flash/debug-only channel: "AT commands are not passed on UART as they
	 * are only available on SPI". So UART cannot carry the NCP protocol and is not an alternative
	 * transport to the SPI link above; do not try to drive the module over it. */

	const struct st67w611_conf ble_conf = {
		.spidev = &spi1_ble.dev,
		.en_gpio = ble_en_gpio,
		.boot_gpio = ble_boot_gpio,
		.rdy_gpio = ble_rdy_gpio,
	};
	if (st67w611_init(&ble, &ble_conf) != ST67W611_RET_OK) {
		return;
	}

	/* Bring BLE up and start advertising this port under its name. */
	st67w611_ble_advertise(&ble, "nwdaq-main-hh1");
}


/**********************************************************************************************************************
 * BQ25798 Li-ion battery charger on the power management I2C bus
 **********************************************************************************************************************/

Bq25798 charger;

Gpio *charge_en_gpio = &(gpioa.pin[2]);

static void port_setup_charger(void) {
	/* CE (charge enable) on PA2 is active low. Drive it low to unblock charging; the charging
	 * itself is then controlled over I2C. */
	charge_en_gpio->vmt->set_mode(charge_en_gpio, MODE_OUTPUT);
	charge_en_gpio->vmt->set(charge_en_gpio, false);

	if (bq25798_init(&charger, &i2c2.bus, BQ25798_I2C_ADDR) != BQ25798_RET_OK) {
		return;
	}

	/* Advertise the battery voltage measurement as a sensor. */
	Sensor *vbat = NULL;
	bq25798_get_battery_voltage(&charger, &vbat);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)vbat, "charger_vbat");

	/* Advertise the battery charging control as a power device. */
	Power *power = NULL;
	bq25798_get_power(&charger, &power);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_POWER, (Interface *)power, "charger");
}


/**********************************************************************************************************************
 * VBUS_LP boost regulator feeding the measurement card
 **********************************************************************************************************************/

Gpio *vbus_lp_en_gpio = &(gpiod.pin[12]);

static void port_setup_vbus_lp(void) {
	/* EN on PD12 is active high. Drive it high to enable the boost regulator that powers the
	 * measurement card. */
	vbus_lp_en_gpio->vmt->set_mode(vbus_lp_en_gpio, MODE_OUTPUT);
	vbus_lp_en_gpio->vmt->set(vbus_lp_en_gpio, true);
}


/**********************************************************************************************************************
 * BQ27441-G1 battery fuel gauge on the power management I2C bus
 **********************************************************************************************************************/

Bq27441 fuel_gauge;

static void port_setup_fuel_gauge(void) {
	if (bq27441_init(&fuel_gauge, &i2c2.bus, BQ27441_I2C_ADDR) != BQ27441_RET_OK) {
		return;
	}

	/* Advertise the fuel gauge measurements as sensors for use in applications. */
	Sensor *sensor = NULL;
	bq27441_get_battery_voltage(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_voltage");

	bq27441_get_battery_current(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_current");

	bq27441_get_state_of_charge(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_soc");

	bq27441_get_state_of_health(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_soh");

	bq27441_get_remaining_capacity(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_remaining");
}


/**********************************************************************************************************************
 * Keypad using a PCAL6408A GPIO expander on I2C1
 **********************************************************************************************************************/

Pcal6408A keypad_gpio;
GpioKeypad keypad;
KeypadLayout keypad_layout;

/* The gpio-keypad driver only reports raw per-pin presses (EV_RAW_x, one per expander pin). The buttons short
 * the expander input to ground, hence the inputs are pulled up and the sense is inverted. */
struct gpio_keypad_key keypad_keys[] = {
	{ .input = &(keypad_gpio.pin[0]), .type = EV_TYPE_RAW, .code = EV_RAW_0, .invert = true },
	{ .input = &(keypad_gpio.pin[1]), .type = EV_TYPE_RAW, .code = EV_RAW_1, .invert = true },
	{ .input = &(keypad_gpio.pin[2]), .type = EV_TYPE_RAW, .code = EV_RAW_2, .invert = true },
	{ .input = &(keypad_gpio.pin[3]), .type = EV_TYPE_RAW, .code = EV_RAW_3, .invert = true },
	{ .input = &(keypad_gpio.pin[4]), .type = EV_TYPE_RAW, .code = EV_RAW_4, .invert = true },
	{ .input = &(keypad_gpio.pin[5]), .type = EV_TYPE_RAW, .code = EV_RAW_5, .invert = true },
	{ .input = &(keypad_gpio.pin[6]), .type = EV_TYPE_RAW, .code = EV_RAW_6, .invert = true },
	{ .input = &(keypad_gpio.pin[7]), .type = EV_TYPE_RAW, .code = EV_RAW_7, .invert = true },
	{ .input = NULL }
};

/* Layout mapping the raw keypad presses to logical key events. Pin-to-key assignment is provisional and will be
 * corrected against the real hardware later. "OK" has no dedicated event code, so it is mapped to EV_KEY_ENTER. */
const struct keypad_layout_item keypad_layout_map[] = {
	{ .in_code = EV_RAW_0, .out_code = EV_KEY_F3 },
	{ .in_code = EV_RAW_1, .out_code = EV_KEY_F4 },
	{ .in_code = EV_RAW_2, .out_code = EV_KEY_RIGHT },
	{ .in_code = EV_RAW_2, .out_code = EV_REL_X, .counter = 1 },
	{ .in_code = EV_RAW_3, .out_code = EV_KEY_ENTER },
	{ .in_code = EV_RAW_4, .out_code = EV_KEY_LEFT },
	{ .in_code = EV_RAW_4, .out_code = EV_REL_X, .counter = -1 },
	{ .in_code = EV_RAW_5, .out_code = EV_KEY_ESC },
	{ .in_code = EV_RAW_6, .out_code = EV_KEY_F1, .out_code_long = EV_KEY_F5 },
	{ .in_code = EV_RAW_7, .out_code = EV_KEY_F2, .out_code_long = EV_KEY_F6 },
	{ .in_code = EV_CODE_NONE }
};


static void port_setup_keypad(void) {
	if (pcal6408a_gpio_init(&keypad_gpio, &i2c1.bus) != PCAL6408A_GPIO_RET_OK) {
		return;
	}

	/* All eight expander pins are wired to the keypad buttons. */
	for (size_t i = 0; i < 8; i++) {
		keypad_gpio.pin[i].vmt->set_mode(&(keypad_gpio.pin[i]), MODE_INPUT);
		keypad_gpio.pin[i].vmt->set_pull(&(keypad_gpio.pin[i]), PULL_UP);
	}

	/* The gpio-keypad produces raw events; the keypad-layout service translates them into the key events
	 * consumed downstream and is the one advertised as the "keypad" event source. */
	gpio_keypad_init(&keypad, keypad_keys);

	const struct keypad_layout_conf keypad_layout_conf = {
		.source = &keypad.event,
		.layout = keypad_layout_map,
		.long_press_ms = 1000,
	};
	keypad_layout_init(&keypad_layout, &keypad_layout_conf);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_EVENT, (Interface *)&keypad_layout.event, "keypad");
}


/**********************************************************************************************************************
 * Piezo beeper driven by TIM1 CH1
 **********************************************************************************************************************/

Stm32Timer beeper_timer;
PwmBeeper beeper;

Gpio *beeper_gpio = &(gpioe.pin[9]);

static void port_setup_beeper(void) {
	/* TIM1_CH1 output on PE9, AF1. */
	beeper_gpio->vmt->set_mode(beeper_gpio, MODE_ALTERNATE);
	beeper_gpio->vmt->set_pinmux(beeper_gpio, 1);

	/* TIM1 is an advanced-control timer on APB2. */
	RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
	stm32_timer_init(&beeper_timer, (void *)TIM1, SystemCoreClock);

	Pwm *pwm = NULL;
	stm32_timer_pwm_init(&beeper_timer, 1, &pwm);
	pwm_beeper_init(&beeper, pwm);

	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_BEEPER, (Interface *)&beeper.beeper, "beeper");

	/* Give a single beep after boot to indicate the firmware is up. */
	//beeper.beeper.vmt->sequence(&beeper.beeper, BEEPER_SEQ_SINGLE_BEEP);
}


static void port_setup_applets(void) {
#if defined(CONFIG_APPLET_HELLO_WORLD)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_world, "hello-world");
#endif
#if defined(CONFIG_APPLET_SETTINGS)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&settings, "settings");
#endif
#if defined(CONFIG_APPLET_HELLO_WORLD)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_world, "hello-world");
#endif
#if defined(CONFIG_APPLET_SETTINGS)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&settings, "settings");
#endif
#if defined(CONFIG_APPLET_HELLO_WORLD)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_world, "hello-world");
#endif
#if defined(CONFIG_APPLET_SETTINGS)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&settings, "settings");
#endif
#if defined(CONFIG_APPLET_HELLO_WORLD)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_world, "hello-world");
#endif
#if defined(CONFIG_APPLET_SETTINGS)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&settings, "settings");
#endif
#if defined(CONFIG_APPLET_LIVE_DATA)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&live_data, "live-data");
#endif
#if defined(CONFIG_APPLET_HELLO_WREN)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_wren, "hello-wren");
#endif
}


int32_t port_init(void) {
	stm32_gpio_init(&gpioa, (void *)GPIOA_BASE);
	stm32_gpio_init(&gpiob, (void *)GPIOB_BASE);
	stm32_gpio_init(&gpioc, (void *)GPIOC_BASE);
	stm32_gpio_init(&gpiod, (void *)GPIOD_BASE);
	stm32_gpio_init(&gpioe, (void *)GPIOE_BASE);

	port_setup_console();
	port_setup_nbus2();
	port_setup_i2c();
	port_setup_pm_i2c();
	port_setup_leds();
	port_setup_lcd();
	port_setup_display();
	port_setup_t1s();
	port_setup_ble();
	port_setup_charger();
	port_setup_vbus_lp();
	port_setup_fuel_gauge();
	port_setup_flash();
	port_setup_keypad();
	port_setup_beeper();
	port_setup_applets();

	return PORT_INIT_OK;
}


/* Configure dedicated timer (TIM6) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {
	RCC->APB1RSTR1 |= RCC_APB1RSTR1_TIM6RST;
	RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_TIM6RST;
	/* The timer should run at 1MHz */
	TIM6->PSC = (SystemCoreClock / 1000000) - 1;
	TIM6->CR1 &= ~TIM_CR1_OPM;
	TIM6->ARR = UINT16_MAX;
	TIM6->CR1 |= TIM_CR1_CEN;
}


uint32_t port_task_timer_get_value(void) {
	return TIM6->CNT;
}
