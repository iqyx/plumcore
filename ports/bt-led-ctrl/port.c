/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * bt-led-ctrl LED controller
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

/* Low level drivers for th STM32G4 family */
#include <services/stm32-system-clock/clock.h>
#include <services/stm32-rtc/rtc.h>
#include <services/stm32-i2c/stm32-i2c.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-adc/stm32-adc.h>
#include <services/stm32-dac/stm32-dac.h>
#include <services/stm32-watchdog/watchdog.h>

/* High level drivers */
#if !defined(CONFIG_APP_BL)
	#include <services/stm32-clock/stm32-clock.h>
	#include <services/generic-power/generic-power.h>
#endif


/**
 * Port specific global variables and singleton instances.
 */

Watchdog watchdog;

#if !defined(CONFIG_APP_BL)
	Stm32Clock cmgr;
#endif


int32_t port_early_init(void) {
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_USART1);
	rcc_periph_clock_enable(RCC_DAC1);

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM6);

	/* ADC is used for PCB temperature measurement */
	rcc_periph_clock_enable(RCC_ADC1);
	/** @todo needed fo G4? */
	RCC_CCIPR |= 3 << 28;

	return PORT_EARLY_INIT_OK;
}


Stm32Uart uart1;
Stream *bt;
static void bt_init(void) {
	/* USART1 RX/TX */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO9 | GPIO10);
	gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO9 | GPIO10);
	gpio_set_af(GPIOA, GPIO_AF7, GPIO9 | GPIO10);

	/* Initialise and configure the UART */
	stm32_uart_init(&uart1, USART1);
	uart1.uart.vmt->set_bitrate(&uart1.uart, 115200);

	nvic_enable_irq(NVIC_USART1_IRQ);
	nvic_set_priority(NVIC_USART1_IRQ, 7 * 16);

	bt = &uart1.stream;
}


void usart1_isr(void) {
	stm32_uart_interrupt_handler(&uart1);
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
	/* LED enable */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0);
	gpio_clear(GPIOB, GPIO0);

	/* BT module UART CTS */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO11);
	gpio_clear(GPIOA, GPIO11);

	/* BT module RST */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO8);
	gpio_clear(GPIOA, GPIO8);
	vTaskDelay(10);
	gpio_set(GPIOA, GPIO8);
}


Stm32Dac dac1_1;

static void led_init(void) {

	/* Enable excitation. */
	stm32_dac_init(&dac1_1, DAC1, DAC_CHANNEL1);
	dac1_1.dac_iface.vmt->set_single(&dac1_1.dac_iface, 0.2f);

	gpio_clear(GPIOB, GPIO0);
}


void bt_command(char *c) {
	bt->vmt->write(bt, c, strlen(c));
	bt->vmt->write(bt, "\r\n", 2);
}


static void bt_read_line(char *buf, size_t max_buf, size_t *len) {

	uint8_t c;
	*len = 0;
	while (true) {
		size_t read = 0;
		stream_ret_t ret = bt->vmt->read_timeout(bt, &c, 1, &read, 10);

		if (ret == STREAM_RET_TIMEOUT) {
			continue;
		}

		if (c == '\n' || c == '\r') {
			if (*len > 0) {
				/* At least some non-whitespace characters were received. End with
				 * reception of the current line. */
				return;
			} else {
				/* If no non-whitespace characters were received yet, just eat
				 * all newline characters until a valid line begins. */
				continue;
			}
		}

		/* A valid character is received. If there is enough buffer space, save it. */
		if (*len < max_buf) {
			buf[*len] = c;
			(*len)++;
		} else {
			/* The line is too long. Drop the current character and wait for the
			 * end of line anyway.*/
			continue;
		}
	}
}


uint32_t hex2int(char *buf, int len) {
	uint32_t r = 0;
	for (int i = 0; i < len; i++) {
		r <<= 4;
		if (buf[i] >= '0' && buf[i] <= '9') {
			r += buf[i] - '0';
		}
		if (buf[i] >= 'a' && buf[i] <= 'f') {
			r += buf[i] - 'a' + 10;
		}
		if (buf[i] >= 'A' && buf[i] <= 'F') {
			r += buf[i] - 'A' + 10;
		}
	}
	return r;
}


int32_t port_init(void) {
	port_setup_default_gpio();

	#if !defined(CONFIG_APP_BL)
		led_init();
		bt_init();
	#endif

	//watchdog_init(&watchdog, 8000, 1);
	//bt_command("AT+CPWROFF");
	vTaskDelay(1000);
	bt_command("AT+UBTLE=2");
	bt_command("AT&W");
	bt_command("AT+CPWROFF");
	vTaskDelay(1000);
	bt_command("AT+UBTLN=\"BT-LED4\"");
	bt_command("AT&W");
	bt_command("AT+CPWROFF");
	vTaskDelay(1000);
	bt_command("AT+UBTGSER=b8580e44828fecabf19929586c2eb5fb");
	bt_command("AT+UBTGCHA=1547fcb1c572db5aeca4813c2bf10072,08,1,1,00,1,1");

	char buf[100];
	size_t read = 0;
	while (true) {
		bt_read_line(buf, sizeof(buf), &read);
		if (!strncmp(buf, "+UUBTGRW", 8) && read == 18) {
			uint8_t val = hex2int(buf + 14, 2);
			if (val == 0) {
				gpio_clear(GPIOB, GPIO0);
			} else {
				dac1_1.dac_iface.vmt->set_single(&dac1_1.dac_iface, 0.3f - val * 0.001);
				gpio_set(GPIOB, GPIO0);
			}
		}
	}

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



