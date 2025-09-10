/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-s2-inc2 inclination sensing module for the S2 platform
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
#include <services/stm32-spi/stm32-spi.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-adc/stm32-adc.h>
#include <services/stm32-dac/stm32-dac.h>
#include <services/stm32-watchdog/watchdog.h>

/* High level drivers */
#if !defined(CONFIG_APP_BL)
	#include <services/stm32-clock/stm32-clock.h>
	#include <services/generic-power/generic-power.h>
	#include <services/adc-sensor/adc-sensor.h>
	#include <services/iis2iclx/iis2iclx.h>
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
	rcc_periph_clock_enable(RCC_SPI1);
	rcc_periph_clock_enable(RCC_SPI3);

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

	/* MUX selection outputs. */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO2);
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO1);
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO2);

	/* Excitation outputs. */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO4 | GPIO5);


}


/**********************************************************************************************************************
 * Liquid sensor excitation
 **********************************************************************************************************************/

Stm32Dac dac1_1;
Stm32Dac dac1_2;

static void liquid_sensor_init(void) {

	/* Enable excitation. */
	stm32_dac_init(&dac1_1, DAC1, DAC_CHANNEL1);
	stm32_dac_init(&dac1_2, DAC1, DAC_CHANNEL2);
	dac1_1.dac_iface.vmt->set_single(&dac1_1.dac_iface, 0.35f);
	dac1_2.dac_iface.vmt->set_single(&dac1_2.dac_iface, 0.65f);

	rcc_periph_clock_enable(RCC_TIM4);
	timer_disable_counter(TIM4);
	timer_set_mode(TIM4, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_disable_preload(TIM4);
	timer_set_prescaler(TIM4, 16 - 1);
	timer_continuous_mode(TIM4);
	timer_set_period(TIM4, 2500 - 1);

	timer_disable_oc_output(TIM4, TIM_OC3);
	timer_disable_oc_preload(TIM4, TIM_OC3);
	timer_set_oc_mode(TIM4, TIM_OC3, TIM_OCM_PWM1);
	timer_set_oc_value(TIM4, TIM_OC3, 1250 - 1);
	timer_enable_irq(TIM4, TIM_DIER_CC3IE);

	timer_disable_oc_output(TIM4, TIM_OC4);
	timer_disable_oc_preload(TIM4, TIM_OC4);
	timer_set_oc_mode(TIM4, TIM_OC4, TIM_OCM_PWM1);
	timer_set_oc_value(TIM4, TIM_OC4, 2300 - 1);
	timer_enable_irq(TIM4, TIM_DIER_CC4IE);

	/* Enable toggling of the excitation outputs. */
	timer_enable_irq(TIM4, TIM_DIER_UIE);

	nvic_enable_irq(NVIC_TIM4_IRQ);
	nvic_set_priority(NVIC_TIM4_IRQ, 5 * 16);
	timer_enable_counter(TIM4);

}


/**********************************************************************************************************************
 * IIS2ICLX
 **********************************************************************************************************************/

Stm32SpiBus spi3;
Stm32SpiDev spi3_accel1;
Iis2Iclx iis2;

static void accel_setup(void) {
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO5);
	gpio_set_af(GPIOB, GPIO_AF6, GPIO5);
	gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO10 | GPIO11);
	gpio_set_af(GPIOC, GPIO_AF6, GPIO10 | GPIO11);
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO6);
	gpio_set(GPIOB, GPIO6);

	stm32_spibus_init(&spi3, SPI3);
	spi3.bus.vmt->set_sck_freq(&spi3.bus, 4e6);
	spi3.bus.vmt->set_mode(&spi3.bus, 0, 0);

	stm32_spidev_init(&spi3_accel1, &spi3.bus, GPIOB, GPIO6);

	iis2iclx_init(&iis2, &(spi3_accel1.dev));
}


/**********************************************************************************************************************
 * Liquid tilt sensor and temp sensor readout using MCP3564
 **********************************************************************************************************************/

Stm32SpiBus spi1;
Stm32SpiDev spi1_adc;
Mcp3564 mcp;
SemaphoreHandle_t adc_meas;
SemaphoreHandle_t adc_ready;
SemaphoreHandle_t value_ready;
SemaphoreHandle_t adc_new_cycle;

#define MUX_CH_INC_X MCP3564_MUX_CH5
#define MUX_CH_INC_Y MCP3564_MUX_CH4
#define MUX_CH_TEMP_X MCP3564_MUX_CH6
#define MUX_CH_TEMP_Y MCP3564_MUX_CH7

const enum mcp3564_mux adc_mux_config[4] = {
	MUX_CH_INC_X,
	MUX_CH_INC_Y,
	MUX_CH_TEMP_X,
	MUX_CH_TEMP_Y,
};

int32_t adc_value[4];
int32_t adc_avg[4];

float mems_acc_x, mems_acc_y, mems_temp;
float mems_acc_x_avg, mems_acc_y_avg, mems_temp_avg;

volatile uint32_t adc_cycle = 0;

static void adc_task(void *p) {

	while (true) {
		uint8_t status = 0;
		int32_t r = 0;

		xSemaphoreTake(adc_new_cycle, portMAX_DELAY);
		/* Try to take the following semaphores. The reason for this is the first one
		 * which should be signalled is adc_new_cycle but that happens during the update
		 * event which happens last. */
		xSemaphoreTake(adc_meas, 0);
		xSemaphoreTake(adc_ready, 0);

		/* Manage the measurement sequence. */
		mcp3564_set_mux(&mcp, MCP3564_MUX_VCM, adc_mux_config[(adc_cycle % 8) / 2]);

		/* Wait for the right time to start the measurement. */
		xSemaphoreTake(adc_meas, portMAX_DELAY);
		gpio_set(GPIOB, GPIO2);
		mcp3564_send_cmd(&mcp, MCP3564_CMD_START, &status, NULL, 0, NULL, 0);

		/* Now we have some time to read MEMS sensors. */
		int16_t ax, ay, at;
		iis2iclx_read(&iis2, &ay, &ax, &at);
		mems_acc_x += ax;
		mems_acc_y += ay;
		mems_temp += at;

		/* Wait until data ready arrives. */
		xSemaphoreTake(adc_ready, portMAX_DELAY);
		mcp3564_read_reg(&mcp, MCP3564_REG_ADCDATA, 4, (uint32_t *)&r, &status);

		/* Process the measurement. */
		if (adc_cycle % 2) {
			adc_value[(adc_cycle % 8) / 2] += r;
		} else {
			adc_value[(adc_cycle % 8) / 2] -= r;
		}

		if ((adc_cycle % 80) == 79) {
			for (size_t i = 0; i < 4; i++) {
				adc_avg[i] = adc_value[i] / 10;
				adc_value[i] = 0;
			}

			mems_acc_x_avg = mems_acc_x / 80.0f;
			mems_acc_y_avg = mems_acc_y / 80.0f;
			mems_temp_avg = mems_temp / 80.0f;
			mems_acc_x = 0.0f;
			mems_acc_y = 0.0f;
			mems_temp = 0.0f;

			xSemaphoreGive(value_ready);
		}
		gpio_clear(GPIOB, GPIO2);
	}
	vTaskDelete(NULL);
}


static void adc_init(void) {
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6 | GPIO7);
	gpio_set_af(GPIOA, GPIO_AF5, GPIO6 | GPIO7);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO3);
	gpio_set_af(GPIOB, GPIO_AF5, GPIO3);
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO4);

	stm32_spibus_init(&spi1, SPI1);
	spi1.bus.vmt->set_sck_freq(&spi1.bus, 4e6);
	spi1.bus.vmt->set_mode(&spi1.bus, 0, 0);

	stm32_spidev_init(&spi1_adc, &spi1.bus, GPIOC, GPIO4);

	mcp3564_init(&mcp, &(spi1_adc.dev));
	mcp3564_set_stp_enable(&mcp, false);
	mcp3564_set_gain(&mcp, MCP3564_GAIN_1);
	mcp3564_set_osr(&mcp, MCP3564_OSR_256);
	mcp3564_set_mux(&mcp, MCP3564_MUX_VCM, MCP3564_MUX_CH4);
	mcp3564_set_irq_mode(&mcp, MCP3564_IRQ_MODE_IRQ);
	mcp3564_set_stp_enable(&mcp, true);
	mcp3564_update(&mcp);

	adc_meas = xSemaphoreCreateBinary();
	adc_ready = xSemaphoreCreateBinary();
	value_ready = xSemaphoreCreateBinary();
	adc_new_cycle = xSemaphoreCreateBinary();
	xTaskCreate(adc_task, "adc-task", configMINIMAL_STACK_SIZE + 256, NULL, 2, NULL);

}


void tim4_isr(void) {
	if (timer_get_flag(TIM4, TIM_SR_CC3IF)) {
		timer_clear_flag(TIM4, TIM_SR_CC3IF);

		BaseType_t woken = pdFALSE;
		xSemaphoreGiveFromISR(adc_meas, &woken);
		portYIELD_FROM_ISR(woken);
	}

	if (timer_get_flag(TIM4, TIM_SR_CC4IF)) {
		timer_clear_flag(TIM4, TIM_SR_CC4IF);

		BaseType_t woken = pdFALSE;
		xSemaphoreGiveFromISR(adc_ready, &woken);
		portYIELD_FROM_ISR(woken);
	}

	if (timer_get_flag(TIM4, TIM_SR_UIF)) {
		timer_clear_flag(TIM4, TIM_SR_UIF);

		adc_cycle++;

		/* Alternate the excitation outputs. Must happen regardless of the measurement sequence. */
		if (adc_cycle % 2) {
			gpio_set(GPIOB, GPIO1);
			gpio_set(GPIOA, GPIO2);
		} else {
			gpio_clear(GPIOB, GPIO1);
			gpio_clear(GPIOA, GPIO2);
		}

		BaseType_t woken = pdFALSE;
		xSemaphoreGiveFromISR(adc_new_cycle, &woken);
		portYIELD_FROM_ISR(woken);
	}



}


static float adc_to_ntc(int32_t adc, float ref) {
	return ref * (adc + 8388608.0f) / (16777215.0f - (adc + 8388608.0f));
}


static float ntc_to_temp(float ntc, float beta, float ref) {
	return 1.0f / (1.0f / (25.0f + 273.15f) + (1.0f / beta) * log(ntc / ref)) - 273.15f;
}


int32_t port_init(void) {
	port_setup_default_gpio();
	console_init();

	#if !defined(CONFIG_APP_BL)

	#endif

	//watchdog_init(&watchdog, 8000, 1);

	#if !defined(CONFIG_APP_BL)
		accel_setup();
		adc_init();
		liquid_sensor_init();

		while (true) {
			xSemaphoreTake(value_ready, portMAX_DELAY);
			float temp_x = ntc_to_temp(adc_to_ntc(adc_avg[2], 10000.0f), 3977.0f, 10000.0f);
			float temp_y = ntc_to_temp(adc_to_ntc(adc_avg[3], 10000.0f), 3977.0f, 10000.0f);;
			u_log(system_log, LOG_TYPE_DEBUG, "%d %d %.3f %.3f %.2f %.2f %.2f", adc_avg[0], adc_avg[1], temp_x, temp_y, mems_acc_x_avg, mems_acc_y_avg, mems_temp_avg);

		}
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



