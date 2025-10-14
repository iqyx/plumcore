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

#include "module_led.h"
#include "interface_led.h"

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

#if !defined(CONFIG_APP_BL)
	#include <services/stm32-clock/stm32-clock.h>
	#include <services/generic-power/generic-power.h>
#endif

/* Libs */
#include <xz.h>

#define MODULE_NAME "port"


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
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO4);
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO5);
	gpio_set(GPIOC, GPIO4);
	gpio_clear(GPIOA, GPIO5);
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


uint8_t bl_xz_in_buf[256];
uint8_t bl_xz_out_buf[256];

struct xz_buf bl_xz_buf = {
	bl_xz_in_buf,
	0,
	0,
	bl_xz_out_buf,
	0,
	256
};
struct xz_dec *bl_xz;

static inline uint32_t get_unaligned_le32(const uint8_t *buf) {
	return (uint32_t)buf[0]
			| ((uint32_t)buf[1] << 8)
			| ((uint32_t)buf[2] << 16)
			| ((uint32_t)buf[3] << 24);
}

static void xz_test(void) {
	xz_crc32_init();
	bl_xz = xz_dec_init(XZ_PREALLOC, 32768);
	u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("xz_dec_init = %p"), bl_xz);
	if (bl_xz == NULL) {
		return;
	}

	size_t pos = 0;
	size_t osize = 0;
	while (true) {
		if (bl_xz_buf.in_pos == bl_xz_buf.in_size) {
			if (lv_update->vmt->read(lv_update, pos, bl_xz_in_buf, 256) != FLASH_RET_OK) {
				u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("flash reading failed"));
				return;
			}


			bl_xz_buf.in_pos = 0;
			bl_xz_buf.in_size = 256;
			pos += 256;
			console->vmt->write(console, "#", 1);

		}

		enum xz_ret ret = xz_dec_run(bl_xz, &bl_xz_buf);

		/* Output full buffer size */
		if (bl_xz_buf.out_pos == bl_xz_buf.out_size) {
			osize += bl_xz_buf.out_pos;
			bl_xz_buf.out_pos = 0;
		}

		if (ret == XZ_OK) {
			continue;
		}

		/* Output the rest. */
		osize += bl_xz_buf.out_pos;

		if (ret == XZ_STREAM_END) {
			u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("stream end, size = %lu"), osize);
			break;
		}

		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("some error ret = %d"), ret);
		break;
	}

	xz_dec_end(bl_xz);
}


int32_t port_init(void) {
	//watchdog_init(&watchdog, 8000, 1);
	port_setup_default_gpio();
	console_init();
	port_flash_init();
	//xz_test();

	#if !defined(CONFIG_APP_BL)
		gpio_set(GPIOA, GPIO5);
		while (true) {
			gpio_toggle(GPIOC, GPIO4);
			vTaskDelay(500);
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



