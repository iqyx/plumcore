/*
 * nucleo-f411re port-specific configuration
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/exti.h>

#include "FreeRTOS.h"
#include "timers.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include "module_led.h"
#include "interface_led.h"
#include "module_usart.h"
#include "interface_stream.h"
#include "interface_spibus.h"
#include "module_spibus_locm3.h"
#include "interface_spidev.h"
#include "module_spidev_locm3.h"
#include "interface_rtc.h"
#include "module_rtc_locm3.h"
#include "interface_rng.h"
#include "module_prng_simple.h"
#include "interfaces/adc.h"
#include "sffs.h"
#include "services/radio-rfm69/rfm69.h"

#include "interfaces/sensor.h"
#include "interfaces/cellular.h"
#include "interfaces/servicelocator.h"
#include "interfaces/radio-mac/client.h"
#include "services/stm32-adc/adc_stm32_locm3.h"
#include "services/stm32-watchdog/watchdog.h"
#include "services/plocator/plocator.h"
#include "services/cli/system_cli_tree.h"
#include "services/stm32-system-clock/clock.h"
#include "services/stm32-rtc/rtc.h"
#include "services/radio-mac-simple/radio-mac-simple.h"


/**
 * Port specific global variables and singleton instances.
 */

/* Old-style HAL */
uint32_t SystemCoreClock;
struct module_led led_stat;
struct module_usart console;
struct module_spibus_locm3 spi2;
struct module_spidev_locm3 spi2_radio1;
struct module_rtc_locm3 rtc1;
struct module_prng_simple prng;
struct sffs fs;

/*New-style HAL and services. */
AdcStm32Locm3 adc1;
Watchdog watchdog;
ServiceCli mqtt_cli;
PLocator plocator;
IServiceLocator *locator;
SystemClock system_clock;
Stm32Rtc rtc;
Rfm69 radio1;
MacSimple mac1;
Radio r;

int32_t port_early_init(void) {
	/* Relocate the vector table first. */
	// SCB_VTOR = 0x00010400;

	/* Select HSE as SYSCLK source, no PLL, 8MHz. */
	// rcc_osc_on(RCC_HSE);
	// rcc_wait_for_osc_ready(RCC_HSE);
	// rcc_set_sysclk_source(RCC_CFGR_SW_HSE);

	rcc_set_hpre(RCC_CFGR_HPRE_DIV_NONE);
	rcc_set_ppre1(RCC_CFGR_PPRE_DIV_NONE);
	rcc_set_ppre2(RCC_CFGR_PPRE_DIV_NONE);

	/* Initialize systick interrupt for FreeRTOS. */
	nvic_set_priority(NVIC_SYSTICK_IRQ, 255);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(15999);
	systick_interrupt_enable();
	systick_counter_enable();

	/* System clock is now at 16MHz (without PLL). */
	SystemCoreClock = 16000000;
	rcc_apb1_frequency = 16000000;
	rcc_apb2_frequency = 16000000;

	/* Initialize all required clocks for GPIO ports. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* Timer 11 needs to be initialized prior to startint the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM11);


	return PORT_EARLY_INIT_OK;
}


int32_t port_init(void) {

	/* Serial console. */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO2 | GPIO3);
	gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO2 | GPIO3);
	gpio_set_af(GPIOA, GPIO_AF7, GPIO2 | GPIO3);

	/* Main system console is created on the first USART interface. Enable
	 * USART clocks and interrupts and start the corresponding HAL module. */
	rcc_periph_clock_enable(RCC_USART2);
	nvic_enable_irq(NVIC_USART2_IRQ);
	nvic_set_priority(NVIC_USART2_IRQ, 6 * 16);
	module_usart_init(&console, "console", USART2);
	hal_interface_set_name(&(console.iface.descriptor), "console");

	/* Console is now initialized, set its stream to be used as u_log output. */
	u_log_set_stream(&(console.iface));

	/* Analog inputs (battery measurement). */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO5 | GPIO6);

	/* First, initialize the service locator service. It is required to
	 * advertise any port-specific devices and interfaces. */
	if (plocator_init(&plocator) == PLOCATOR_RET_OK) {
		locator = plocator_iface(&plocator);
	}

	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_STREAM,
		(Interface *)&console.iface.descriptor,
		"console"
	);

	/* Initialize the Real-time clock. */
	module_rtc_locm3_init(&rtc1, "rtc1");
	hal_interface_set_name(&(rtc1.iface.descriptor), "rtc1");
	u_log_set_rtc(&(rtc1.iface));
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_RTC,
		(Interface *)&rtc1.iface.descriptor,
		"rtc1"
	);

	/* Status LEDs. */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO5);
	module_led_init(&led_stat, "led_stat");
	module_led_set_port(&led_stat, GPIOA, GPIO5);
	interface_led_loop(&(led_stat.iface), 0x11f1);
	hal_interface_set_name(&(led_stat.iface.descriptor), "led_stat");
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_LED,
		(Interface *)&led_stat.iface.descriptor,
		"led_stat"
	);

	module_prng_simple_init(&prng, "prng");
	hal_interface_set_name(&(prng.iface.descriptor), "prng");
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_RNG,
		(Interface *)&prng.iface.descriptor,
		"rng1"
	);

	/** @todo the watchdog is port-dependent. Rename the module accordingly and keep it here. */
	watchdog_init(&watchdog, 20000, 0);

	rcc_periph_clock_enable(RCC_TIM2);
	nvic_enable_irq(NVIC_TIM2_IRQ);
	system_clock_init(&system_clock, TIM2, 15, UINT32_MAX);
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_CLOCK,
		&system_clock.iface.interface,
		"main"
	);

	stm32_rtc_init(&rtc);
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_CLOCK,
		&rtc.iface.interface,
		"rtc"
	);

	/* Configure GPIO for radio & flash SPI bus (SPI2), enable SPI2 clock and
	 * run spibus driver using libopencm3 to access it. */
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO13 | GPIO14 | GPIO15);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO13 | GPIO14 | GPIO15);
	gpio_set_af(GPIOB, GPIO_AF5, GPIO13 | GPIO14 | GPIO15);
	rcc_periph_clock_enable(RCC_SPI2);
	module_spibus_locm3_init(&spi2, "spi2", SPI2);
	hal_interface_set_name(&(spi2.iface.descriptor), "spi2");

	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO12);
	gpio_set(GPIOB, GPIO12);
	module_spidev_locm3_init(&spi2_radio1, "spi2_radio1", &(spi2.iface), GPIOB, GPIO12);
	hal_interface_set_name(&(spi2_radio1.iface.descriptor), "spi2_radio1");

	rfm69_init(&radio1, &spi2_radio1.iface);
	rfm69_start(&radio1);

	radio_open(&r, &radio1.radio);
	radio_set_tx_power(&r, 0);
	radio_set_frequency(&r, 434132000);
	radio_set_bit_rate(&r, 25000);

	mac_simple_init(&mac1, &r);
	mac1.low_power = false;
	mac_simple_set_clock(&mac1, &system_clock.iface);

	sffs_init(&fs);
	/* Do not mount the filesystem as there is no flash available. */

	return PORT_INIT_OK;
}


int32_t system_test(void) {

	RadioMac mac;
	radio_mac_open(&mac, &mac1.iface, 1);

	while (true) {
		uint8_t buf[64];
		uint32_t source;
		size_t len = 0;
		// radio_mac_receive(&mac, &source, buf, sizeof(buf), &len);
		// u_log(system_log, LOG_TYPE_DEBUG, "mac rx packet len=%d", len);
		radio_mac_send(&mac, 0, "abcd", 4);
		vTaskDelay(1000);
		u_log(system_log, LOG_TYPE_DEBUG, "rxp=%d rxm=%d rssi=%d err=%d qlen=%u", mac1.nbtable.items[0].rxpackets, mac1.nbtable.items[0].rxmissed, (int32_t)(mac1.nbtable.items[0].rssi_dbm), mac1.slot_start_time_error_ema_us, rmac_slot_queue_len(&mac1.slot_queue));
	}

	return 0;
}


void tim2_isr(void) {
	if (TIM_SR(TIM2) & TIM_SR_UIF) {
		timer_clear_flag(TIM2, TIM_SR_UIF);
		system_clock_overflow_handler(&system_clock);
	}
}


/* Configure dedicated timer (tim3) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {
	rcc_periph_reset_pulse(RST_TIM11);
	/* The timer should run at 1MHz */
	timer_set_prescaler(TIM11, 1599);
	timer_continuous_mode(TIM11);
	timer_set_period(TIM11, UINT16_MAX);
	timer_enable_counter(TIM11);
}


uint32_t port_task_timer_get_value(void) {
	return timer_get_counter(TIM11);
}


void usart2_isr(void) {
	module_usart_interrupt_handler(&console);
}

