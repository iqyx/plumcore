/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nwdaq-br28-fdc port-specific configuration
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
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

#include <main.h>
#include "port.h"

#include "module_led.h"
#include "interface_led.h"

#include "interface_spibus.h"
#include "module_spibus_locm3.h"
#include "interface_flash.h"
#include "module_spi_flash.h"
#include "interface_spidev.h"
#include "module_spidev_locm3.h"

#include <interfaces/sensor.h>
#include <interfaces/servicelocator.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>

#include <services/cli/system_cli_tree.h>
#include <services/stm32-system-clock/clock.h>
#include <services/stm32-rtc/rtc.h>
#include <services/stm32-i2c/stm32-i2c.h>
#include <services/stm32-uart/stm32-uart.h>


/**
 * GPIO definitions
 */
#define CAN_SHDN_PORT GPIOA
#define CAN_SHDN_PIN GPIO10
#define CAN_RX_PORT GPIOA
#define CAN_RX_PIN GPIO11
#define CAN_TX_PORT GPIOA
#define CAN_TX_PIN GPIO12
#define CAN_AF GPIO_AF9

#define EXC_EN_PORT GPIOA
#define EXC_EN_PIN GPIO8

/* ADC_MCLK on TIM2, channel 3 */
#define ADC_MCLK_PORT GPIOB
#define ADC_MCLK_PIN GPIO10
#define ADC_MCLK_AF GPIO_AF1

/* ADC_IRQ on TIM2, channel 4 */
#define ADC_IRQ_PORT GPIOB
#define ADC_IRQ_PIN GPIO11
#define ADC_IRQ_AF GPIO_AF1

#define LED_STAT_PORT GPIOB
#define LED_STAT_PIN GPIO6

#define LED_ERROR_PORT GPIOC
#define LED_ERROR_PIN GPIO10

#define ADC_CS_PORT GPIOB
#define ADC_CS_PIN GPIO12

#define VDDA_SW_EN_PORT GPIOA
#define VDDA_SW_EN_PIN GPIO1

#define VREF1_DAC_PORT GPIOA
#define VREF1_DAC_PIN GPIO4
#define VREF2_DAC_PORT GPIOA
#define VREF2_DAC_PIN GPIO5

#define VREF_M (0)
#define VREF_P (4095)

#define VREF1_SEL_PORT GPIOB
#define VREF1_SEL_PIN GPIO1
#define VREF2_SEL_PORT GPIOB
#define VREF2_SEL_PIN GPIO2

#define MUX_EN_PORT GPIOB
#define MUX_EN_PIN GPIO0

#define MUX_A0_PORT GPIOC
#define MUX_A0_PIN GPIO4
#define MUX_A1_PORT GPIOA
#define MUX_A1_PIN GPIO7
#define MUX_A2_PORT GPIOA
#define MUX_A2_PIN GPIO6


/**
 * Port specific global variables and singleton instances.
 */

/* Old-style HAL */
uint32_t SystemCoreClock;

/*New-style HAL and services. */
#if defined(CONFIG_SERVICE_STM32_WATCHDOG)
	#include "services/stm32-watchdog/watchdog.h"
	Watchdog watchdog;
#endif
SystemClock system_clock;
Stm32Rtc rtc;


/* PLL configuration for a 19.2 MHz XTAL. We are using such a weird frequency to possibly
 * use it as a MCLK source for the Microchip ADC. MCU is run at 64 MHz because we need
 * a round clock for CAN-FD too (which, as it seems, conflicts with the ADC's MCLK requirements). */
static const struct rcc_clock_scale hse_192_to_64 = {
	.pllm = 6,
	.plln = 40,
	.pllp = 2,
	.pllq = 2,
	.pllr = 2,
	.pll_source = RCC_PLLCFGR_PLLSRC_HSE,
	.hpre = RCC_CFGR_HPRE_NODIV,
	.ppre1 = RCC_CFGR_PPREx_NODIV,
	.ppre2 = RCC_CFGR_PPREx_NODIV,
	.vos_scale = PWR_SCALE1,
	.boost = false,
	.flash_config = FLASH_ACR_DCEN | FLASH_ACR_ICEN,
	.flash_waitstates = 2,
	.ahb_frequency  = 64e6,
	.apb1_frequency = 64e6,
	.apb2_frequency = 64e6,
};


int32_t port_early_init(void) {
	/* Relocate the vector table first if required. */
	#if defined(CONFIG_RELOCATE_VECTOR_TABLE)
		SCB_VTOR = CONFIG_VECTOR_TABLE_ADDRESS;
	#endif

	rcc_osc_on(RCC_HSE);
	rcc_wait_for_osc_ready(RCC_HSE);
	rcc_clock_setup_pll(&hse_192_to_64);
	rcc_set_sysclk_source(RCC_PLL);

	/* Running on 64 MHz PLL now */

	#if defined(CONFIG_STM32_DEBUG_IN_STOP)
		/* Leave SWD running in STOP1 mode for development. */
		DBGMCU_CR |= DBGMCU_CR_STOP;
	#endif

	/* Enable LSE. */
	// rcc_osc_on(RCC_LSE);
	// rcc_wait_for_osc_ready(RCC_LSE);


	/* Initialize all required clocks for GPIO ports. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM6);

	return PORT_EARLY_INIT_OK;
}


/*********************************************************************************************************************
 * System serial console initialization
 *********************************************************************************************************************/

#if defined(CONFIG_NWDAQ_BR28_FDC_ENABLE_SERIAL_CONSOLE)
	Stm32Uart console;
	static void console_init(void) {
		gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO2 | GPIO3);
		gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO2 | GPIO3);
		gpio_set_af(GPIOA, GPIO_AF7, GPIO2 | GPIO3);

		/* Main system console is created on the first USART interface. Enable
		 * USART clocks and interrupts and start the corresponding HAL module. */
		rcc_periph_clock_enable(RCC_USART2);

		nvic_enable_irq(NVIC_USART2_IRQ);
		nvic_set_priority(NVIC_USART2_IRQ, 6 * 16);
		stm32_uart_init(&console, USART2);
		console.uart.vmt->set_bitrate(&console.uart, CONFIG_NWDAQ_BR28_FDC_CONSOLE_SPEED);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&console.stream, "console");

		/* Console is now initialized, set its stream to be used as u_log output. */
		u_log_set_stream(&(console.stream));
	}


	void usart2_isr(void) {
		stm32_uart_interrupt_handler(&console);
	}
#endif


#if defined(CONFIG_NWDAQ_BR28_FDC_ENABLE_LEDS)
	struct module_led led_status;
	struct module_led led_error;

	static void led_init(void) {
		gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO6);
		module_led_init(&led_status, "led_status");
		module_led_set_port(&led_status, GPIOB, GPIO6);
		interface_led_loop(&led_status.iface, 0x222f);
		hal_interface_set_name(&(led_status.iface.descriptor), "led_status");
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_LED, (Interface *)&led_status.iface.descriptor, "led_status");

		gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO10);
		module_led_init(&led_error, "led_error");
		module_led_set_port(&led_error, GPIOC, GPIO10);
		interface_led_loop(&led_error.iface, 0xf);
		hal_interface_set_name(&(led_error.iface.descriptor), "led_error");
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_LED, (Interface *)&led_error.iface.descriptor, "led_error");
	}
#endif


void vPortSetupTimerInterrupt(void);
void vPortSetupTimerInterrupt(void) {
	/* Initialize systick interrupt for FreeRTOS. */
	nvic_set_priority(NVIC_SYSTICK_IRQ, 255);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(64e3 - 1);
	systick_interrupt_enable();
	systick_counter_enable();
}


static void port_setup_default_gpio(void) {
	/* CAN port. Enable the transceiver permanently (SHDN = L). */
	gpio_mode_setup(CAN_SHDN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, CAN_SHDN_PIN);
	gpio_clear(CAN_SHDN_PORT, CAN_SHDN_PIN);
	gpio_mode_setup(CAN_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, CAN_RX_PIN);
	gpio_set_af(CAN_RX_PORT, CAN_AF, CAN_RX_PIN);
	gpio_mode_setup(CAN_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, CAN_TX_PIN);
	gpio_set_af(CAN_TX_PORT, CAN_AF, CAN_TX_PIN);

	gpio_mode_setup(EXC_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, EXC_EN_PIN);
	gpio_set(EXC_EN_PORT, EXC_EN_PIN);

	gpio_mode_setup(ADC_MCLK_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, ADC_MCLK_PIN);
	gpio_set_af(ADC_MCLK_PORT, ADC_MCLK_AF, ADC_MCLK_PIN);

	gpio_mode_setup(ADC_IRQ_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, ADC_IRQ_PIN);

	/* ADC SPI port */
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO13 | GPIO14 | GPIO15);
	gpio_set_af(GPIOB, GPIO_AF5, GPIO13 | GPIO14 | GPIO15);

	/* ADC CS port */
	gpio_set(ADC_CS_PORT, ADC_CS_PIN);
	gpio_mode_setup(ADC_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, ADC_CS_PIN);

	/* Enable VDDA_SW for op-amps and MUXes */
	gpio_mode_setup(VDDA_SW_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, VDDA_SW_EN_PIN);
	gpio_clear(VDDA_SW_EN_PORT, VDDA_SW_EN_PIN);

	/* DAC outputs for the reference driver power op-amp */
	gpio_mode_setup(VREF1_DAC_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, VREF1_DAC_PIN);
	gpio_mode_setup(VREF2_DAC_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, VREF2_DAC_PIN);

	dac_enable(DAC1, DAC_CHANNEL1);
	dac_load_data_buffer_single(DAC1, VREF_M, DAC_ALIGN_RIGHT12, DAC_CHANNEL1);
	dac_enable(DAC1, DAC_CHANNEL2);
	dac_load_data_buffer_single(DAC1, VREF_P, DAC_ALIGN_RIGHT12, DAC_CHANNEL2);


	/* VREF inversion MUX */
	gpio_mode_setup(VREF1_SEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, VREF1_SEL_PIN);
	gpio_mode_setup(VREF2_SEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, VREF2_SEL_PIN);

	/* Input MUX */
	gpio_mode_setup(MUX_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, MUX_EN_PIN);
	gpio_set(MUX_EN_PORT, MUX_EN_PIN);
	gpio_mode_setup(MUX_A0_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, MUX_A0_PIN);
	gpio_mode_setup(MUX_A1_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, MUX_A1_PIN);
	gpio_mode_setup(MUX_A2_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, MUX_A2_PIN);
}


int32_t port_init(void) {
	port_setup_default_gpio();
	console_init();
	stm32_rtc_init(&rtc);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CLOCK, &rtc.iface.interface, "rtc");
	#if defined(CONFIG_NWDAQ_BR28_FDC_ENABLE_LEDS)
		led_init();
	#endif
	return PORT_INIT_OK;
}


void tim2_isr(void) {
	if (TIM_SR(TIM2) & TIM_SR_UIF) {
		timer_clear_flag(TIM2, TIM_SR_UIF);
		system_clock_overflow_handler(&system_clock);
	}
}


/* Configure dedicated timer (TIM6) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {
	rcc_periph_reset_pulse(RST_TIM6);
	/* The timer should run at 1MHz */
	timer_set_prescaler(TIM6, 64 - 1);
	timer_continuous_mode(TIM6);
	timer_set_period(TIM6, UINT16_MAX);
	timer_enable_counter(TIM6);
}


uint32_t port_task_timer_get_value(void) {
	return timer_get_counter(TIM6);
}


void port_enable_swd(bool e) {

}

