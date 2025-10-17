/* SPDX-License-Identifier: proprietary
 *
 * nwdaq-if-rs44
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
#include <services/stm32-spi/stm32-spi.h>
#include <services/stm32-dac/stm32-dac.h>

/* High level drivers */
#include <services/stm32-flash/stm32-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>
#include <services/i2c-eeprom/i2c-eeprom.h>
#include <services/stm32-clock/stm32-clock.h>

#if !defined(CONFIG_APP_BL)
	#include <services/nbus2-switch/nbus2-switch.h>
	#include <services/generic-power/generic-power.h>
	#include <services/nbus2/nbus2.h>
	#include <services/nbus-flash/nbus-flash.h>

	/* System services */
	Stm32Clock cmgr;

	/* Applets */
#endif


/**
 * Port specific global variables and singleton instances.
 */

uint32_t SystemCoreClock;
Watchdog watchdog;
// Stm32Rtc rtc;

#if !defined(CONFIG_APP_BL)
	Nbus2Switch sw;
	NbusFlash flash_proto;
#endif


int32_t port_early_init(void) {

	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOD);
	rcc_periph_clock_enable(RCC_GPIOE);
	rcc_periph_clock_enable(RCC_USART3);

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM6);

	/** @todo needed fo G4? */
	RCC_CCIPR |= 3 << 28;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * NBUS2 backplane init
 **********************************************************************************************************************/

#if !defined(CONFIG_APP_BL)
Stm32Uart nbus_bp_uart;
Nbus nbus_bp;

static void nbus_bp_port_init(void) {
	/* USART2 RX/TX */
	gpio_mode_setup(GPIOD, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO5 | GPIO6);
	gpio_set_output_options(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO5 | GPIO6);
	gpio_set_af(GPIOD, GPIO_AF7, GPIO5 | GPIO6);

	/* Driver enable (shutdown), permanently on. */
	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO4);
	gpio_clear(GPIOD, GPIO4);

	rcc_periph_clock_enable(RCC_USART2);

	stm32_uart_init(&nbus_bp_uart, USART2);
	stm32_uart_set_rto(&nbus_bp_uart, true);
	nbus_bp_uart.uart.vmt->set_bitrate(&nbus_bp_uart.uart, 1000000);

	nvic_enable_irq(NVIC_USART2_IRQ);
	nvic_set_priority(NVIC_USART2_IRQ, 5 * 16);

	nbus_init(&nbus_bp, &nbus_bp_uart.stream);
	nbus_set_mac_key(&nbus_bp, (uint8_t *)"abcd", 4);
}

void usart2_isr(void) {
	stm32_uart_interrupt_handler(&nbus_bp_uart);
}
#endif


/**********************************************************************************************************************
 * NBUS2 front panel ports init
 **********************************************************************************************************************/

#if !defined(CONFIG_APP_BL)
Stm32Uart nbus_uart[4];
Nbus nbus[4];

static void nbus_port0_init(void) {
	/* USART3 RX/TX */
	gpio_mode_setup(GPIOD, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO8 | GPIO9);
	gpio_set_output_options(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO8 | GPIO9);
	gpio_set_af(GPIOD, GPIO_AF7, GPIO8 | GPIO9);

	/* Driver enable. */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO14);
	gpio_clear(GPIOB, GPIO14);

	rcc_periph_clock_enable(RCC_USART3);

	stm32_uart_init(&(nbus_uart[0]), USART3);
	stm32_uart_set_rto(&(nbus_uart[0]), true);
	stm32_uart_set_de(&(nbus_uart[0]), GPIOB, GPIO14);
	nbus_uart[0].uart.vmt->set_bitrate(&(nbus_uart[0].uart), 1000000);

	nvic_enable_irq(NVIC_USART3_IRQ);
	nvic_set_priority(NVIC_USART3_IRQ, 5 * 16);

	nbus_init(&(nbus[0]), &(nbus_uart[0]).stream);
	nbus_set_mac_key(&(nbus[0]), (uint8_t *)"abcd", 4);
}

void usart3_isr(void) {
	stm32_uart_interrupt_handler(&(nbus_uart[0]));
}
#endif


void vPortSetupTimerInterrupt(void);
void vPortSetupTimerInterrupt(void) {
	/* Initialize systick interrupt for FreeRTOS. */
	nvic_set_priority(NVIC_SYSTICK_IRQ, 255);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(16e3 - 1);
	systick_interrupt_enable();
	systick_counter_enable();
}


static void port_setup_default_gpio(void) {
	/* LED1A, bootloader red LED */
	gpio_mode_setup(GPIOE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO10);
	gpio_set(GPIOE, GPIO10);

	gpio_mode_setup(GPIOE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO11);
	gpio_clear(GPIOE, GPIO11);

	gpio_mode_setup(GPIOE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
	gpio_clear(GPIOE, GPIO12);

	gpio_mode_setup(GPIOE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13);
	gpio_clear(GPIOE, GPIO13);

}


/**********************************************************************************************************************
 * Buck converter init
 **********************************************************************************************************************/
#if !defined(CONFIG_APP_BL)
Stm32Dac dac1_1;
GenericPower buck;
GenericPower out_port[4];

static void buck_dac_init(void) {
	generic_power_init(&buck);
	generic_power_set_vref(&buck, 3.3f);

	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO4);
	rcc_periph_clock_enable(RCC_DAC1);
	stm32_dac_init(&dac1_1, DAC1, DAC_CHANNEL1);
	generic_power_set_voltage_dac(&buck, &dac1_1.dac_iface, NULL);

	/* Setup excitation enable GPIO output. Not inverted. */
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO5);
	gpio_clear(GPIOC, GPIO5);
	generic_power_set_enable_gpio(&buck, GPIOC, GPIO5, false);

	/* Enable the power converter. */
	buck.power.vmt->set_voltage(&buck.power, 0.70f);
	vTaskDelay(10);
	buck.power.vmt->enable(&buck.power, true);
	vTaskDelay(10);

	for (uint32_t i = 0; i < 4; i++) {
		generic_power_init(&(out_port[i]));
	}

	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO11);
	gpio_clear(GPIOB, GPIO11);
	generic_power_set_enable_gpio(&out_port[1], GPIOB, GPIO11, false);

	for (uint32_t i = 0; i < 4; i++) {
		out_port[i].power.vmt->enable(&(out_port[i].power), true);
	}
}
#endif


Stm32Flash iflash;
FlashVolStatic pv_iflash;

Flash *lv_bl;
Flash *lv_conf;
Flash *lv_mib;
Flash *lv_app;
Flash *lv_update;

static void port_flash_init(void) {
	stm32_flash_init(&iflash);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)&iflash.flash, "flash0");

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


int32_t port_init(void) {
	port_setup_default_gpio();
	port_flash_init();

	#if !defined(CONFIG_APP_BL)
		gpio_clear(GPIOE, GPIO10);

		stm32_clock_init(&cmgr, STM32_CLOCK_LEVEL_MEDIUM_PERF);
		stm32_clock_wait_init_done(&cmgr);

		buck_dac_init();

		/** @todo initialize ports in a more sane way */
		nbus_bp_port_init();
		nbus_port0_init();

		for (uint32_t i = 0; i < 10; i++) {
			gpio_toggle(GPIOE, GPIO11);
			vTaskDelay(100);
		}

		/** @todo move to the application controlling the board. Catch traffic
		 *        destined to this device and make its API accessible. */
		struct nbus_socket *flash_proto_socket = nbus_socket_allocate(&nbus_bp);
		nbus_socket_bind(flash_proto_socket, (uint8_t[4]){0x00, 0x00, 0x00, 0x25}, 1);

		/* Access flash partitions to allow updating. */
		nbus_flash_init(&flash_proto, &flash_proto_socket->datagram);

		/** @todo create nbus2 switch inside of the application, not here. */
		nbus2_switch_init(&sw);

		struct nbus_socket *bp_socket = nbus_socket_allocate(&nbus_bp);
		nbus2_switch_add_port(&sw, &bp_socket->datagram, 0, 0);
		struct nbus_socket *port0_socket = nbus_socket_allocate(&(nbus[0]));
		nbus2_switch_add_port(&sw, &port0_socket->datagram, GPIOE, GPIO11);
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



