/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-s2-main-g4 main module for the S2 platform
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/dac.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/fdcan.h>

#include <main.h>
#include "port.h"

#include <interfaces/sensor.h>
#include <interfaces/servicelocator.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>
#include <interfaces/adc.h>
#include <interfaces/flash.h>

/* Low level drivers for th STM32G4 family */
#include <services/stm32-system-clock/clock.h>
#include <services/stm32-rtc/rtc.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-watchdog/watchdog.h>

/* High level drivers */
#include <services/stm32-flash/stm32-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>

#if defined(CONFIG_APP_BL)
	#include <xz.h>
#else
	#include <blake2s.h>
	#include <services/nbus2/nbus2.h>
	#include <services/stm32-clock/stm32-clock.h>
	#include <services/generic-power/generic-power.h>
	#include <interfaces/led.h>
	#include <interfaces/led-sequences.h>
	#include <services/stm32-gpio/stm32-gpio.h>
	#include <services/gpio-led/gpio-led.h>
#endif

#define MODULE_NAME "port"


/**
 * Port specific global variables and singleton instances.
 */

Watchdog watchdog;

#if !defined(CONFIG_APP_BL)
	Stm32Clock cmgr;
	Stm32Gpio gpioa;
	Stm32Gpio gpiob;
	Stm32Gpio gpioc;
	GpioLed led_stat;
	GpioLed led_error;
#endif


int32_t port_early_init(void) {
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_USART1);
	rcc_periph_clock_enable(RCC_SPI1);
	rcc_periph_clock_enable(RCC_SPI3);

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM6);

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

Stm32Uart uart1;
Stream *console;
static void console_init(void) {
	/* USART1 RX/TX */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO9 | GPIO10);
	gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO9 | GPIO10);
	gpio_set_af(GPIOA, GPIO_AF7, GPIO9 | GPIO10);

	/* Initialise and configure the UART */
	stm32_uart_init(&uart1, USART1);
	uart1.uart.vmt->set_bitrate(&uart1.uart, 115200);

	nvic_enable_irq(NVIC_USART1_IRQ);
	nvic_set_priority(NVIC_USART1_IRQ, 7 * 16);

	/* Advertise the console stream output and set it as default for log output. */
	console = &uart1.stream;
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)console, "console");
	u_log_set_stream(console);
}

void usart1_isr(void) {
	stm32_uart_interrupt_handler(&uart1);
}


/**********************************************************************************************************************
 * Stacking connector nbus2 port init
 **********************************************************************************************************************/

Stm32Uart nbus2_uart;
static void nbus2_init(void) {
	/* USART3 TX half-duplex */
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO10);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, GPIO10);
	gpio_set_af(GPIOB, GPIO_AF7, GPIO10);

	rcc_periph_clock_enable(RCC_USART3);
	USART_CR3(USART3) |= USART_CR3_HDSEL;
	USART_CR3(USART3) |= USART_CR3_OVRDIS;

	stm32_uart_init(&nbus2_uart, USART3);
	stm32_uart_set_rto(&nbus2_uart, true);
	nbus2_uart.uart.vmt->set_bitrate(&nbus2_uart.uart, 250000);

	nvic_enable_irq(NVIC_USART3_IRQ);
	nvic_set_priority(NVIC_USART3_IRQ, 7 * 16);

	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&nbus2_uart.stream, "nbus2_stream");
}

void usart3_isr(void) {
	stm32_uart_interrupt_handler(&nbus2_uart);
}



void vPortSetupTimerInterrupt(void);
void vPortSetupTimerInterrupt(void) {
	/* Initialize systick interrupt for FreeRTOS. */
	nvic_set_priority(NVIC_SYSTICK_IRQ, 255);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(16000UL - 1);
	systick_interrupt_enable();
	systick_counter_enable();
}


Stm32Flash iflash;
FlashVolStatic pv_iflash;

Flash *lv_bl;
Flash *lv_conf;
Flash *lv_mib;
Flash *lv_app;
Flash *lv_update;

static void port_flash_init(void) {
	stm32_flash_init(&iflash);

	flash_vol_static_init(&pv_iflash, &iflash.flash);
	flash_vol_static_create(&pv_iflash, "bootloader", 0,          60 * 1024,  &lv_bl);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_bl, "bootloader");

	flash_vol_static_create(&pv_iflash, "bootconf",   60 * 1024,  2 * 1024,   &lv_conf);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_conf, "bootconf");

	flash_vol_static_create(&pv_iflash, "mib",        62 * 1024,  2 * 1024,   &lv_mib);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_mib, "mib");

	flash_vol_static_create(&pv_iflash, "app",        64 * 1024,  128 * 1024, &lv_app);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_app, "app");

	flash_vol_static_create(&pv_iflash, "update",     192 * 1024, 64 * 1024,  &lv_update);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_update, "update");
}

struct nbus_socket *socket;

int32_t port_init(void) {
	watchdog_init(&watchdog, 8000, 1);
	console_init();
	port_flash_init();

	stm32_gpio_init(&gpioa, STM32_PORTA);
	stm32_gpio_init(&gpiob, STM32_PORTB);
	stm32_gpio_init(&gpioc, STM32_PORTC);

	/* LED1 */
	gpioc.pin[4].vmt->set_mode(&(gpioc.pin[4]), MODE_OUTPUT);
	gpiob.pin[0].vmt->set_mode(&(gpiob.pin[0]), MODE_OUTPUT);
	gpiob.pin[1].vmt->set_mode(&(gpiob.pin[1]), MODE_OUTPUT);

	/* LED2 */
	gpioa.pin[5].vmt->set_mode(&(gpioa.pin[5]), MODE_OUTPUT);
	gpioa.pin[6].vmt->set_mode(&(gpioa.pin[6]), MODE_OUTPUT);
	gpioa.pin[7].vmt->set_mode(&(gpioa.pin[7]), MODE_OUTPUT);

	gpio_led_init(&led_stat, &(gpioc.pin[4]), &(gpiob.pin[0]), &(gpiob.pin[1]));
	gpio_led_invert(&led_stat, true);
	led_stat.led.vmt->sequence(&led_stat.led, LED_SEQ_HEARTBEAT);

	gpio_led_init(&led_error, &(gpioa.pin[7]), &(gpioa.pin[6]), &(gpioa.pin[5]));
	gpio_led_invert(&led_error, true);
	led_error.led.vmt->set(&led_error.led, LED_COLOR_RGB(0, 255, 255));

	#if !defined(CONFIG_APP_BL)
		nbus2_init();
	#endif

	return PORT_INIT_OK;
}


/* Configure dedicated timer (TIM6) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {
	rcc_periph_reset_pulse(RST_TIM6);
	/* The timer should run at 1MHz */
	timer_set_prescaler(TIM6, 16 - 1);
	timer_continuous_mode(TIM6);
	timer_set_period(TIM6, UINT16_MAX);
	timer_enable_counter(TIM6);
}


uint32_t port_task_timer_get_value(void) {
	return timer_get_counter(TIM6);
}



