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
#include <stdio.h>
#include <math.h>

#include <stm32g4xx.h>

#include <main.h>
#include "port.h"

#include <interfaces/servicelocator.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>

/* Low level drivers for the STM32G4 family */
#include <services/stm32-gpio/stm32-gpio.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-watchdog/watchdog.h>

#include <services/stm32-flash/stm32-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>

#include <interfaces/led-sequences.h>
#include <services/gpio-led/gpio-led.h>


#define MODULE_NAME "port"


/**
 * Port specific global variables and singleton instances.
 */

uint32_t SystemCoreClock;

Watchdog watchdog;

Stm32Gpio gpioa;
Stm32Gpio gpiob;
Stm32Gpio gpioc;


int32_t port_early_init(void) {
	SystemCoreClock = 16e6;

	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

Stm32Uart uart1;
static void console_init(void) {
	/* USART1 RX/TX on PA9/PA10, AF7. */
	gpioa.pin[9].vmt->set_mode(&(gpioa.pin[9]), MODE_ALTERNATE);
	gpioa.pin[9].vmt->set_pull(&(gpioa.pin[9]), PULL_UP);
	gpioa.pin[9].vmt->set_pinmux(&(gpioa.pin[9]), 7);
	gpioa.pin[10].vmt->set_mode(&(gpioa.pin[10]), MODE_ALTERNATE);
	gpioa.pin[10].vmt->set_pull(&(gpioa.pin[10]), PULL_UP);
	gpioa.pin[10].vmt->set_pinmux(&(gpioa.pin[10]), 7);

	/* Initialise and configure the UART */
	stm32_uart_init(&uart1, (void *)USART1);
	uart1.uart.vmt->set_bitrate(&uart1.uart, 115200);

	NVIC_EnableIRQ(USART1_IRQn);
	NVIC_SetPriority(USART1_IRQn, 7);

	/* Advertise the console stream output and set it as default for log output. */
	Stream *console = &uart1.stream;
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)console, "console");
	u_log_set_stream(console);
}


void usart1_isr(void);
void usart1_isr(void) {
	stm32_uart_interrupt_handler(&uart1);
}


/**********************************************************************************************************************
 * Stacking connector nbus2 port init
 **********************************************************************************************************************/

#if !defined(CONFIG_APP_BL)
Stm32Uart nbus2_uart;
static void nbus2_init(void) {
	RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN;

	/* USART3 TX on PB10, AF7, open-drain half-duplex. */
	gpiob.pin[10].vmt->set_mode(&(gpiob.pin[10]), MODE_ALTERNATE);
	gpiob.pin[10].vmt->set_otype(&(gpiob.pin[10]), OTYPE_OD);
	gpiob.pin[10].vmt->set_pull(&(gpiob.pin[10]), PULL_UP);
	gpiob.pin[10].vmt->set_pinmux(&(gpiob.pin[10]), 7);

	stm32_uart_init(&nbus2_uart, (void *)USART3);
	stm32_uart_set_swmode(&nbus2_uart);
	stm32_uart_set_rto(&nbus2_uart, true);
	nbus2_uart.uart.vmt->set_bitrate(&nbus2_uart.uart, 250000);

	NVIC_EnableIRQ(USART3_IRQn);
	NVIC_SetPriority(USART3_IRQn, 7);

	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&nbus2_uart.stream, "nbus2_stream");
}


void usart3_isr(void);
void usart3_isr(void) {
	stm32_uart_interrupt_handler(&nbus2_uart);
}
#endif


void vPortSetupTimerInterrupt(void);
void vPortSetupTimerInterrupt(void) {
	/* Initialize systick interrupt for FreeRTOS. */
	NVIC_SetPriority(SysTick_IRQn, 15);
	SysTick->LOAD = 16000UL - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}


/**********************************************************************************************************************
 * Internal flash memory initialisation
 **********************************************************************************************************************/

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


/**********************************************************************************************************************
 * LED initialization
 **********************************************************************************************************************/

GpioLed led_stat;
GpioLed led_error;

static void led_init(void) {
	/* Status RGB LED on PC4/PB0/PB1. */
	gpioc.pin[4].vmt->set_mode(&(gpioc.pin[4]), MODE_OUTPUT);
	gpiob.pin[0].vmt->set_mode(&(gpiob.pin[0]), MODE_OUTPUT);
	gpiob.pin[1].vmt->set_mode(&(gpiob.pin[1]), MODE_OUTPUT);

	/* Error RGB LED on PA7/PA6/PA5. */
	gpioa.pin[5].vmt->set_mode(&(gpioa.pin[5]), MODE_OUTPUT);
	gpioa.pin[6].vmt->set_mode(&(gpioa.pin[6]), MODE_OUTPUT);
	gpioa.pin[7].vmt->set_mode(&(gpioa.pin[7]), MODE_OUTPUT);

	gpio_led_init(&led_stat, &(gpioc.pin[4]), &(gpiob.pin[0]), &(gpiob.pin[1]));
	gpio_led_invert(&led_stat, true);
	led_stat.led.vmt->sequence(&led_stat.led, LED_SEQ_HEARTBEAT);

	gpio_led_init(&led_error, &(gpioa.pin[7]), &(gpioa.pin[6]), &(gpioa.pin[5]));
	gpio_led_invert(&led_error, true);
	led_error.led.vmt->set(&led_error.led, LED_COLOR_RGB(0, 255, 255));
}


int32_t port_init(void) {
	//watchdog_init(&watchdog, 8000, 1);

	stm32_gpio_init(&gpioa, (void *)0x48000000);
	stm32_gpio_init(&gpiob, (void *)0x48000400);
	stm32_gpio_init(&gpioc, (void *)0x48000800);

	console_init();
	port_flash_init();
	led_init();

	#if !defined(CONFIG_APP_BL)
		nbus2_init();
	#endif

	return PORT_INIT_OK;
}


/* Configure dedicated timer (TIM6) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {
	RCC->APB1RSTR1 |= RCC_APB1RSTR1_TIM6RST;
	RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_TIM6RST;
	/* The timer should run at 1MHz */
	TIM6->PSC = 16 - 1;
	TIM6->CR1 &= ~TIM_CR1_OPM;
	TIM6->ARR = UINT16_MAX;
	TIM6->CR1 |= TIM_CR1_CEN;
}


uint32_t port_task_timer_get_value(void) {
	return TIM6->CNT;
}
