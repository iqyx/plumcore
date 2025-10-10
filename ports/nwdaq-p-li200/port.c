/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-p-li200 battery plugin unit
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

#include "module_led.h"
#include "interface_led.h"

#include <interfaces/sensor.h>
#include <interfaces/servicelocator.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>
#include <interfaces/adc.h>

/* Low level drivers for th STM32G4 family */
#include <services/stm32-system-clock/clock.h>
#include <services/stm32-rtc/rtc.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-watchdog/watchdog.h>


/* High level drivers */
#if !defined(CONFIG_APP_BL)
	#include <services/stm32-clock/stm32-clock.h>
	#include <services/generic-power/generic-power.h>
	#include <services/stm32-i2c/stm32-i2c.h>
	#include <services/bq76922/bq76922.h>
#endif


/**
 * Port specific global variables and singleton instances.
 */

Watchdog watchdog;


#if !defined(CONFIG_APP_BL)
	Stm32Clock cmgr;
#endif


#define CE_PORT GPIOA
#define CE_PIN GPIO7

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
	Stream *console = &uart1.stream;
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)console, "console");
	u_log_set_stream(console);
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
	/* LEDs */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO1 | GPIO2 | GPIO3 | GPIO4 | GPIO5);
	gpio_set(GPIOA, GPIO1 | GPIO2 | GPIO3 | GPIO4 | GPIO5);
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO6);
	gpio_clear(GPIOA, GPIO6);

	/* Charge enable */
	gpio_mode_setup(CE_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, CE_PIN);
	gpio_set_output_options(CE_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, CE_PIN);
	gpio_clear(CE_PORT, CE_PIN);


}


Stm32I2c i2c2;
Bq76922 bq;
static void port_i2c_init(void) {
	rcc_periph_clock_enable(RCC_I2C2);
	nvic_enable_irq(NVIC_I2C2_EV_IRQ);
	nvic_set_priority(NVIC_I2C2_EV_IRQ, 7 * 16);

	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO8 | GPIO9);
	gpio_set_output_options(GPIOA, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, GPIO8 | GPIO9);
	gpio_set_af(GPIOA, GPIO_AF4, GPIO8 | GPIO9);

	stm32_i2c_init(&i2c2, I2C2);
}


void i2c2_ev_isr(void) {
	stm32_i2c_irq_handler(&i2c2);
}


static void write_chg(uint8_t reg, uint8_t val) {
	uint8_t txdata[2] = {reg, val};
	i2c2.bus.transfer(i2c2.bus.parent, 0x6b, txdata, 2, NULL, 0);
}

static uint8_t read_chg(uint8_t reg) {
	uint8_t reg_val = 0;
	i2c2.bus.transfer(i2c2.bus.parent, 0x6b, &reg, 1, &reg_val, 1);
	return reg_val;
}

static uint16_t read_chg16(uint8_t reg) {
	uint16_t reg_val = 0;
	i2c2.bus.transfer(i2c2.bus.parent, 0x6b, &reg, 1, (void *)&reg_val, 2);
	return reg_val;
}

static void write_bat(uint8_t reg, uint8_t val) {
	uint8_t txdata[2] = {reg, val};
	i2c2.bus.transfer(i2c2.bus.parent, 0x08, txdata, 2, NULL, 0);
}

static uint8_t read_bat(uint8_t reg) {
	uint8_t reg_val = 0;
	i2c2.bus.transfer(i2c2.bus.parent, 0x08, &reg, 1, &reg_val, 1);
	return reg_val;
}

static uint16_t read_bat16(uint8_t reg) {
	uint16_t reg_val = 0;
	i2c2.bus.transfer(i2c2.bus.parent, 0x08, &reg, 1, (void *)&reg_val, 2);
	return reg_val;
}




int32_t port_init(void) {
	port_setup_default_gpio();
	console_init();
	watchdog_init(&watchdog, 8000, 1);

	#if !defined(CONFIG_APP_BL)
		port_i2c_init();
		bq76922_init(&bq, &i2c2.bus);

		/* Disable PG and STAT */
		write_chg(0x18, 0xf0);

		/* Enable ADC */
		write_chg(0x2b, 0x80);

		/* Reverse mode input current limit */
		write_chg(0x0a, 0x20 << 2);
		write_chg(0x0b, 0x00);

		/* Bus voltage */
		write_chg(0x0c, 0x00);
		write_chg(0x0d, 0x14);

		/* Charge current limit */
		write_chg(0x02, 0x0f << 2);
		write_chg(0x03, 0x00);

		/* VIN DPM */
		write_chg(0x06, 0x0f << 2);
		write_chg(0x07, 0x00);

		/* Limit battery current in reverse mode to 5 A */
		//write_chg(0x62, 0xc2);

		/* Configure the battery management IC. */
		bq76922_mac(&bq, BQ76922_CONTROL_SLEEP_DISABLE, NULL, 0, NULL, 0);
		vTaskDelay(10);

		bq76922_mac(&bq, BQ76922_CONTROL_SET_CFGUPDATE, NULL, 0, NULL, 0);
		vTaskDelay(10);

		bq76922_mac(&bq, BQ76922_CONTROL_ALL_FETS_ON, NULL, 0, NULL, 0);
		vTaskDelay(10);

		/* Change charge thresholds */
		uint8_t occ_threshold = 10;
		bq76922_mac(&bq, BQ76922_DATA_OCC_THRESHOLD, &occ_threshold, sizeof(occ_threshold), NULL, 0);

		/* Change connected cells */
		uint8_t vcell_mode[2] = {0x0b, 0x00};
		bq76922_mac(&bq, BQ76922_DATA_VCELL_MODE, vcell_mode, sizeof(vcell_mode), NULL, 0);

		vTaskDelay(10);
		bq76922_mac(&bq, BQ76922_CONTROL_EXIT_CFGUPDATE, NULL, 0, NULL, 0);

		gpio_clear(GPIOA, GPIO1);
		while (true) {
			write_chg(0x17, 0xe9);
			uint16_t vac_adc = read_chg16(0x31);
			if ((vac_adc * 2) < 26500) {
				/* Enable reverse mode. */
				write_chg(0x19, 0x21);
				gpio_set(GPIOA, GPIO1);
			} else {
				/* Disable reverse mode. */
				write_chg(0x19, 0x20);
				gpio_clear(GPIOA, GPIO1);
			}
			//vTaskDelay(10);
			//uint16_t r = read_bat16(0x7f);

			//uint8_t safety_status_a = read_bat(0x02);
			//uint8_t safety_status_b = read_bat(0x04);
			//uint8_t safety_status_c = read_bat(0x06);

			//uint8_t pf_status_a = read_bat(0x0b);
			//uint8_t pf_status_b = read_bat(0x0d);
			//uint8_t pf_status_c = read_bat(0x0f);
			//uint8_t pf_status_d = read_bat(0x11);

			uint16_t cell1 = read_bat16(0x14);
			uint16_t cell2 = read_bat16(0x16);
			uint16_t cell3 = read_bat16(0x18);
			uint16_t cell4 = read_bat16(0x1a);
			uint16_t cell5 = read_bat16(0x1c);

			//uint16_t bat_status = read_bat16(0x12);
			//uint8_t fet_status = read_bat(0x7f);
			//if ((fet_status & 0x05) == 0x05) {
				//gpio_clear(GPIOA, GPIO1);
			//} else {
				//gpio_set(GPIOA, GPIO1);
			//}

			//uint16_t vbat = read_chg16(0x33);
			uint8_t r = read_chg(0x21);
			switch (r & 0x7) {
				case 0:
				default:
					gpio_clear(GPIOA, GPIO2 | GPIO3 | GPIO4 | GPIO5);
					break;
				case 7:
				case 6:
				case 5:
					gpio_set(GPIOA, GPIO5 | GPIO4 | GPIO3 | GPIO2);
					break;
				case 4:
					gpio_toggle(GPIOA, GPIO5 | GPIO4 | GPIO3 | GPIO2);
					break;
				case 3:
					gpio_toggle(GPIOA, GPIO4 | GPIO3 | GPIO2);
					gpio_clear(GPIOA, GPIO5);
					break;
				case 2:
					gpio_toggle(GPIOA, GPIO3 | GPIO2);
					gpio_clear(GPIOA, GPIO4 | GPIO5);
					break;
				case 1:
					gpio_toggle(GPIOA, GPIO2);
					gpio_clear(GPIOA, GPIO3 | GPIO4 | GPIO5);
					break;
			}

			gpio_toggle(GPIOA, GPIO6);
			//vTaskDelay(20);
		}
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



