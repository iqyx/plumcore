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

#include <services/adc-mcp3564/mcp3564.h>
#include <services/adc-composite/adc-composite.h>
#include <services/stm32-watchdog/watchdog.h>
// #include <services/locm3-mux/locm3-mux.h>
#include <services/stm32-dac/stm32-dac.h>
#include <services/generic-power/generic-power.h>

/**
 * Port specific global variables and singleton instances.
 */

Watchdog watchdog;
Stm32Rtc rtc;
/* This is somewhat mandatory as the main purpose of the device is to measure something. */
struct module_spibus_locm3 spi2;
struct module_spidev_locm3 spi2_adc;
Mcp3564 mcp;
// Locm3Mux muxp, muxm;
AdcComposite adc;
Stm32Dac dac1_1;
Stm32Dac dac1_2;
GenericPower exc_power;


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
	rcc_osc_on(RCC_HSE);
	rcc_wait_for_osc_ready(RCC_HSE);
	rcc_clock_setup_pll(&hse_192_to_64);
	rcc_set_sysclk_source(RCC_PLL);

	rcc_osc_on(RCC_LSE);
	/** @todo fails intermittently */
	// rcc_wait_for_osc_ready(RCC_LSE);

	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM6);

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

#if defined(CONFIG_NWDAQ_BR28_FDC_ENABLE_SERIAL_CONSOLE)
	Stm32Uart uart2;
	static void console_init(void) {
		/* Set the corresponding GPIO to alternate mode and enable USART2 clock. */
		gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO2 | GPIO3);
		gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO2 | GPIO3);
		gpio_set_af(GPIOA, GPIO_AF7, GPIO2 | GPIO3);
		rcc_periph_clock_enable(RCC_USART2);

		nvic_enable_irq(NVIC_USART2_IRQ);
		nvic_set_priority(NVIC_USART2_IRQ, 6 * 16);

		/* Initialise and configure the UART */
		stm32_uart_init(&uart2, USART2);
		uart2.uart.vmt->set_bitrate(&uart2.uart, CONFIG_NWDAQ_BR28_FDC_CONSOLE_SPEED);

		/* Advertise the console stream output and set it as default for log output. */
		Stream *console = &uart2.stream;
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)console, "console");
		u_log_set_stream(console);
	}


	void usart2_isr(void) {
		stm32_uart_interrupt_handler(&uart2);
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


/**********************************************************************************************************************
 * Main ADC initialisation
 **********************************************************************************************************************/

const struct adc_composite_channel adc_channels[] = {
	{
		.name = "ch1",
		.muxes = &(const struct adc_composite_mux[]) {
			{
				.mux = NULL,
			},
		},
		.number = 1,
		.ac_excitation = true,
	}, {
		.name = "ch2",
		.muxes = &(const struct adc_composite_mux[]) {
			{
				.mux = NULL,
			},
		},
		.number = 2,
		.ac_excitation = true,
	}, {
		.name = "ch3",
		.muxes = &(const struct adc_composite_mux[]) {
			{
				.mux = NULL,
			},
		},
		.number = 3,
		.ac_excitation = true,
	}, {
		.name = NULL,
	},
};

static void adc_init(void) {
	rcc_periph_clock_enable(RCC_SPI2);
	/* GPIO is already initialised */
	module_spibus_locm3_init(&spi2, "spi2", SPI2);
	hal_interface_set_name(&(spi2.iface.descriptor), "spi2");

	module_spidev_locm3_init(&spi2_adc, "spi2-adc", &(spi2.iface), ADC_CS_PORT, ADC_CS_PIN);
	hal_interface_set_name(&(spi2_adc.iface.descriptor), "spi2-adc");

	mcp3564_init(&mcp, &(spi2_adc.iface));
	mcp3564_set_stp_enable(&mcp, false);
	mcp3564_set_gain(&mcp, MCP3564_GAIN_16);
	mcp3564_set_osr(&mcp, MCP3564_OSR_8192);
	mcp3564_set_mux(&mcp, MCP3564_MUX_CH0, MCP3564_MUX_CH1);
	mcp3564_update(&mcp);

	adc_composite_init(&adc, NULL);
	adc.adc = &mcp;
	adc.channels = &adc_channels;
	adc_composite_set_exc_power(&adc, &exc_power.power);

	adc_composite_start_cont(&adc);
}

/**********************************************************************************************************************
 * Sensor excitation initialisation
 **********************************************************************************************************************/

static void exc_init(void) {
	/* Excitation is handled as a generic power device which can be enabled/disabled, it's voltage/polarity set. */
	generic_power_init(&exc_power);
	generic_power_set_vref(&exc_power, 3.0f);

	/* Enable STM32 DAC and create a stm32-dac service per channel. It is used to set the excitation power
	 * device output voltage. */
	gpio_mode_setup(VREF1_DAC_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, VREF1_DAC_PIN);
	gpio_mode_setup(VREF2_DAC_PORT, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, VREF2_DAC_PIN);
	rcc_periph_clock_enable(RCC_DAC1);
	stm32_dac_init(&dac1_1, DAC1, DAC_CHANNEL1);
	stm32_dac_init(&dac1_2, DAC1, DAC_CHANNEL2);
	generic_power_set_voltage_dac(&exc_power, &dac1_1.dac_iface, &dac1_2.dac_iface);

	/* Setup excitation enable GPIO output. Not inverted. */
	gpio_mode_setup(EXC_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, EXC_EN_PIN);
	gpio_clear(EXC_EN_PORT, EXC_EN_PIN);
	generic_power_set_enable_gpio(&exc_power, EXC_EN_PORT, EXC_EN_PIN, false);

}


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
	gpio_clear(EXC_EN_PORT, EXC_EN_PIN);

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
	gpio_clear(VREF1_DAC_PORT, VREF1_DAC_PIN);
	gpio_mode_setup(VREF1_DAC_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, VREF1_DAC_PIN);
	gpio_clear(VREF2_DAC_PORT, VREF2_DAC_PIN);
	gpio_mode_setup(VREF2_DAC_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, VREF2_DAC_PIN);

	/* VREF inversion MUX */
	gpio_mode_setup(VREF1_SEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, VREF1_SEL_PIN);
	gpio_mode_setup(VREF2_SEL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, VREF2_SEL_PIN);

	/* Input MUX */
	gpio_mode_setup(MUX_EN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, MUX_EN_PIN);
	gpio_set(MUX_EN_PORT, MUX_EN_PIN);
	gpio_mode_setup(MUX_A0_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, MUX_A0_PIN);
	gpio_mode_setup(MUX_A1_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, MUX_A1_PIN);
	gpio_mode_setup(MUX_A2_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, MUX_A2_PIN);
	gpio_set(MUX_A0_PORT, MUX_A0_PIN);

}


int32_t port_init(void) {
	port_setup_default_gpio();
	#if defined(CONFIG_NWDAQ_BR28_FDC_ENABLE_SERIAL_CONSOLE)
		console_init();
	#endif
	stm32_rtc_init(&rtc);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CLOCK, &rtc.iface.interface, "rtc");
	#if defined(CONFIG_NWDAQ_BR28_FDC_ENABLE_LEDS)
		led_init();
	#endif
	exc_init();
	adc_init();

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
	timer_set_prescaler(TIM6, 64 - 1);
	timer_continuous_mode(TIM6);
	timer_set_period(TIM6, UINT16_MAX);
	timer_enable_counter(TIM6);
}


uint32_t port_task_timer_get_value(void) {
	return timer_get_counter(TIM6);
}



