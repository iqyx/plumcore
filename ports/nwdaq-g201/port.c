/*
 * nwdaq-g201 port-specific configuration
 *
 * Copyright (C) 2018, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

#include "main.h"

#include "module_led.h"
#include "interface_led.h"
#include "module_usart.h"
#include "interface_stream.h"
#include "interface_rtc.h"
#include "module_rtc_locm3.h"
#include "module_prng_simple.h"
#include "interface_spibus.h"
#include "module_spibus_locm3.h"
#include "interface_spidev.h"
#include "module_spidev_locm3.h"

#include "interfaces/sensor.h"
#include "interfaces/servicelocator.h"
#include "services/plocator/plocator.h"
#include "services/cli/system_cli_tree.h"
#include "services/stm32-system-clock/clock.h"
#include "services/stm32-rtc/rtc.h"


/**
 * Port specific global variables and singleton instances.
 */

/* Old-style HAL */
uint32_t SystemCoreClock;
struct module_led led_stat;
struct module_usart console;
struct module_rtc_locm3 rtc1;

struct module_spibus_locm3 spi1;
struct module_spidev_locm3 spi1_radio1;
struct module_spidev_locm3 spi1_flash1;


/*New-style HAL and services. */

#if defined(CONFIG_SERVICE_STM32_WATCHDOG)
	#include "services/stm32-watchdog/watchdog.h"
	Watchdog watchdog;
#endif

PLocator plocator;
IServiceLocator *locator;
SystemClock system_clock;
Stm32Rtc rtc;


#include "services/radio-rfm69/rfm69.h"
#include "services/radio-mac-simple/radio-mac-simple.h"
#include "interfaces/radio-mac/client.h"
Rfm69 radio1;
MacSimple mac1;
Radio r;

#if defined(CONFIG_SERVICE_SPI_FLASH)
	#include "services/spi-flash/spi-flash.h"
	#include "interfaces/flash.h"
	SpiFlash spi_flash1;
#endif

#if defined(CONFIG_NWDAQ_G201_BAT_VOLTAGE)
	#include "services/stm32-adc/adc_stm32_locm3.h"
	#include "services/adc-sensor/adc-sensor.h"
	AdcStm32Locm3 adc1;
	AdcSensor sensor_bat_voltage;
#endif


int32_t port_early_init(void) {
	/* Relocate the vector table first if required. */
	#if defined(CONFIG_RELOCATE_VECTOR_TABLE)
		SCB_VTOR = CONFIG_VECTOR_TABLE_ADDRESS;
	#endif

	rcc_set_hpre(RCC_CFGR_HPRE_NODIV);
	rcc_set_ppre1(RCC_CFGR_PPRE1_NODIV);
	rcc_set_ppre2(RCC_CFGR_PPRE2_NODIV);

	/** @todo configurable */
	rcc_set_msi_range(RCC_CR_MSIRANGE_4MHZ);
	SystemCoreClock = 4000000;


	rcc_apb1_frequency = SystemCoreClock;
	rcc_apb2_frequency = SystemCoreClock;

	/* Initialize systick interrupt for FreeRTOS. */
	nvic_set_priority(NVIC_SYSTICK_IRQ, 255);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(SystemCoreClock / 1000 - 1);
	systick_interrupt_enable();
	systick_counter_enable();


	/* Initialize all required clocks for GPIO ports. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* Turn off sensor voltage regulator. */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO15);
	gpio_clear(GPIOA, GPIO15);

	/* Timer 11 needs to be initialized prior to startint the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM6);

	return PORT_EARLY_INIT_OK;
}


int32_t port_init(void) {

	/* Serial console. */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO2 | GPIO3);
	gpio_set_output_options(GPIOA, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, GPIO2 | GPIO3);
	gpio_set_af(GPIOA, GPIO_AF7, GPIO2 | GPIO3);

	/* Main system console is created on the first USART interface. Enable
	 * USART clocks and interrupts and start the corresponding HAL module. */
	rcc_periph_clock_enable(RCC_USART2);
	nvic_enable_irq(NVIC_USART2_IRQ);
	nvic_set_priority(NVIC_USART2_IRQ, 6 * 16);
	module_usart_init(&console, "console", USART2);
	module_usart_set_baudrate(&console, 9600);
	hal_interface_set_name(&(console.iface.descriptor), "console");

	/* Console is now initialized, set its stream to be used as u_log output. */
	u_log_set_stream(&(console.iface));

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

	#if defined(CONFIG_NWDAQ_G201_ENABLE_LED)
		/* Status LEDs. */
		gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO8);
		module_led_init(&led_stat, "led_stat");
		module_led_set_port(&led_stat, GPIOA, GPIO8);
		interface_led_loop(&(led_stat.iface), 0x11f1);
		hal_interface_set_name(&(led_stat.iface.descriptor), "led_stat");
		iservicelocator_add(
			locator,
			ISERVICELOCATOR_TYPE_LED,
			(Interface *)&led_stat.iface.descriptor,
			"led_stat"
		);
	#endif

	#if defined(CONFIG_SERVICE_STM32_WATCHDOG)
		watchdog_init(&watchdog, 20000, 0);
	#endif

	#if defined(CONFIG_NWDAQ_G201_BAT_VOLTAGE)
		/* Battery voltage is measured by a resistor divider connected
		 * to GPIO port PA1, which is ADC1 analog input channel 6.
		 * Divider = 470, multiplier = 1470. */
		gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO1);
		adc_stm32_locm3_init(&adc1, ADC1);

		adc_sensor_init(
			&sensor_bat_voltage,
			&adc1.iface,
			6,
			&(ISensorInfo) {
				.quantity_name = "Voltage",
				.quantity_symbol = "U",
				.unit_name = "Volt",
				.unit_symbol = "V"
			},
			1470,
			470
		);
		iservicelocator_add(
			locator,
			ISERVICELOCATOR_TYPE_SENSOR,
			&sensor_bat_voltage.iface.interface,
			"bat-voltage"
		);
	#endif

	rcc_periph_clock_enable(RCC_TIM2);
	rcc_periph_reset_pulse(RST_TIM2);
	nvic_enable_irq(NVIC_TIM2_IRQ);
	system_clock_init(&system_clock, TIM2, SystemCoreClock / 1000000 - 1, UINT32_MAX);
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

	/* The SPI bus contains two devices - SX1231 radio transceiver and a
	 * SPI NOR flash memory. The bus is always configured and enabled. */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO5 | GPIO6 | GPIO7);
	gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO5 | GPIO6 | GPIO7);
	gpio_set_af(GPIOA, GPIO_AF5, GPIO5 | GPIO6 | GPIO7);
	rcc_periph_clock_enable(RCC_SPI1);
	rcc_periph_reset_pulse(RST_SPI1);
	module_spibus_locm3_init(&spi1, "spi1", SPI1);
	hal_interface_set_name(&(spi1.iface.descriptor), "spi1");

	/* SX1231 radio. */
	#if defined(CONFIG_NWDAQ_G201_ENABLE_RADIO)
		gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO4);
		gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO4);
		gpio_set(GPIOA, GPIO4);
		module_spidev_locm3_init(&spi1_radio1, "spi1_radio1", &(spi1.iface), GPIOA, GPIO4);
		hal_interface_set_name(&(spi1_radio1.iface.descriptor), "spi1_radio1");

		rfm69_init(&radio1, &spi1_radio1.iface);
		rfm69_start(&radio1);

		radio_open(&r, &radio1.radio);
		radio_set_tx_power(&r, 0);
		radio_set_frequency(&r, 434200000);
		radio_set_bit_rate(&r, 25000);

		mac_simple_init(&mac1, &r);
		mac1.low_power = true;
		mac_simple_set_mcs(&mac1, &mcs_GMSK03_100K);
		mac_simple_set_clock(&mac1, &system_clock.iface);
		/** @todo configure MAC elsewhere. */
	#endif

	/* SPI flash. */
	#if defined(CONFIG_NWDAQ_G201_ENABLE_FLASH)
		gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0);
		gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO0);
		module_spidev_locm3_init(&spi1_flash1, "spi1_flash1", &(spi1.iface), GPIOB, GPIO0);
		hal_interface_set_name(&(spi1_flash1.iface.descriptor), "spi1_flash1");

		if (spi_flash_init(&spi_flash1, &(spi1_flash1.iface)) == SPI_FLASH_RET_OK) {
			iservicelocator_add(
				locator,
				ISERVICELOCATOR_TYPE_FLASH,
				&(spi_flash_interface(&spi_flash1)->interface),
				"spi-flash1"
			);
		}
	#endif

	return PORT_INIT_OK;
}


/** @todo remove */
int32_t system_test(void) {

	RadioMac mac;
	radio_mac_open(&mac, &mac1.iface, 1);

	while (true) {
		radio_mac_send(&mac, 0, "abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd", 8);
		vTaskDelay(400);
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
	rcc_periph_reset_pulse(RST_TIM6);
	timer_set_prescaler(TIM6, SystemCoreClock / 10000 - 1);
	timer_continuous_mode(TIM6);
	timer_set_period(TIM6, UINT16_MAX);
	timer_enable_counter(TIM6);
}


uint32_t port_task_timer_get_value(void) {
	return timer_get_counter(TIM6);
}


void usart2_isr(void) {
	module_usart_interrupt_handler(&console);
}

