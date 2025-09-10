/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-br28-fdc port-specific configuration
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

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

#include "module_led.h"
#include "interface_led.h"

#include "interface_flash.h"
#include "module_spi_flash.h"

#include <interfaces/sensor.h>
#include <interfaces/servicelocator.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>
#include <interfaces/adc.h>

/* Low level drivers for th STM32G4 family */
#include <services/stm32-system-clock/clock.h>
#include <services/stm32-rtc/rtc.h>
#include <services/stm32-i2c/stm32-i2c.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-adc/stm32-adc.h>
#include <services/stm32-watchdog/watchdog.h>

/* High level drivers */
#if !defined(CONFIG_APP_BL)
	#include <services/stm32-clock/stm32-clock.h>
	#include <services/generic-power/generic-power.h>
	#include <services/adc-sensor/adc-sensor.h>
#endif


/**
 * Port specific global variables and singleton instances.
 */

Watchdog watchdog;

#if !defined(CONFIG_APP_BL)
	Stm32Clock cmgr;
#endif


int32_t port_early_init(void) {
	iwdg_set_period_ms(8000);
	iwdg_start();

	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM6);

	/* ADC is used for PCB temperature measurement */
	rcc_periph_clock_enable(RCC_ADC1);
	/** @todo needed fo G4? */
	RCC_CCIPR |= 3 << 28;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

/** @todo unused, remove */
Stm32Uart uart2;
static void console_init(void) {
	/* Initialise and configure the UART */
	stm32_uart_init(&uart2, USART2);
	uart2.uart.vmt->set_bitrate(&uart2.uart, 115200);

	nvic_enable_irq(NVIC_USART2_IRQ);
	nvic_set_priority(NVIC_USART2_IRQ, 7 * 16);

	/* Advertise the console stream output and set it as default for log output. */
	Stream *console = &uart2.stream;
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)console, "console");
	u_log_set_stream(console);
}


void usart2_isr(void) {
	stm32_uart_interrupt_handler(&uart2);
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


static void port_setup_default_gpio(void) {
	/* Power switch outputs */
	gpio_mode_setup(P1_H_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, P1_H_EN_PIN);
	gpio_clear(P1_H_EN_PORT, P1_H_EN_PIN);

	gpio_mode_setup(P1_L_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, P1_L_EN_PIN);
	gpio_clear(P1_L_EN_PORT, P1_L_EN_PIN);

	gpio_mode_setup(P2_H_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, P2_H_EN_PIN);
	gpio_clear(P2_H_EN_PORT, P2_H_EN_PIN);

	gpio_mode_setup(P2_L_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, P2_L_EN_PIN);
	gpio_clear(P2_L_EN_PORT, P2_L_EN_PIN);

	gpio_mode_setup(VBUS_H_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, VBUS_H_EN_PIN);
	gpio_clear(VBUS_H_EN_PORT, VBUS_H_EN_PIN);

}


int32_t port_init(void) {
	port_setup_default_gpio();
	console_init();

	#if !defined(CONFIG_APP_BL)
	#endif

	//watchdog_init(&watchdog, 8000, 1);

	#if !defined(CONFIG_APP_BL)
		/* Enable front panel port 1. */
		//gpio_set(P1_L_EN_PORT, P1_L_EN_PIN);
		//gpio_set(P1_H_EN_PORT, P1_H_EN_PIN);

		/* Enable backplane VBUS_HP port. */
		gpio_set(VBUS_H_EN_PORT, VBUS_H_EN_PIN);
		gpio_set(P1_L_EN_PORT, P1_L_EN_PIN);
		gpio_set(P1_H_EN_PORT, P1_H_EN_PIN);

	#endif

	return PORT_INIT_OK;
}


void tim2_isr(void) {
	if (TIM_SR(TIM2) & TIM_SR_UIF) {
		timer_clear_flag(TIM2, TIM_SR_UIF);
		// system_clock_overflow_handler(&system_clock);
	}
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



