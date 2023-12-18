/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nwdaq-if-canlp port-specific configuration
 *
 * Copyright (c) 2022-2023, Marek Koza (qyx@krtko.org)
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

#include <interfaces/servicelocator.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>

#include <services/cli/system_cli_tree.h>

/* Low level drivers for the STM32G4 family */
#include <services/stm32-rtc/rtc.h>
#include <services/stm32-i2c/stm32-i2c.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-adc/stm32-adc.h>
#include <services/stm32-watchdog/watchdog.h>
#include <services/stm32-fdcan/stm32-fdcan.h>

/* High level drivers */
#include <services/nbus/nbus.h>
#include <services/nbus/nbus-root.h>
#include <services/nbus/nbus-log.h>
#include <services/nbus-switch/nbus-switch.h>

/**
 * Port specific global variables and singleton instances.
 */

Watchdog watchdog;
Stm32Rtc rtc;


int32_t port_early_init(void) {
	rcc_osc_on(RCC_HSE);
	rcc_wait_for_osc_ready(RCC_HSE);
	rcc_set_sysclk_source(RCC_HSE);

	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM6);

	/* FDCAN interface */
	rcc_periph_clock_enable(RCC_FDCAN);
	rcc_periph_clock_enable(SCC_FDCAN);

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * NBUS/CAN-FD interface
 **********************************************************************************************************************/

Stm32Fdcan can1;
Stm32Fdcan can2;
Stm32Fdcan can3;
Nbus nbus;
NbusRoot root_channel;
NbusLog log_channel;
NbusSwitch switch1;
static void can_init(void) {
	/*************** backplane CAN1 ******************************************/
	rcc_periph_reset_pulse(RST_FDCAN);
	fdcan_init(CAN1, FDCAN_CCCR_INIT_TIMEOUT);
	fdcan_set_can(CAN1, true, false, true, false, 1, 8, 5, (16e6 / (CAN1_BITRATE) / 16) - 1);
	fdcan_set_fdcan(CAN1, true, true, 1, 8, 5, (16e6 / (CAN1_BITRATE) / 16) - 1);

	FDCAN_IE(CAN1) |= FDCAN_IE_RF0NE;
	FDCAN_ILE(CAN1) |= FDCAN_ILE_INT0;
	fdcan_start(CAN1, FDCAN_CCCR_INIT_TIMEOUT);
	stm32_fdcan_init(&can1, CAN1);
	/* TIL: first set the interrupt priority, THEN enable it. */
	/* FDCAN1 has INTR0/INTR1 swapped! */
	nvic_set_priority(NVIC_FDCAN1_INTR1_IRQ, 6 * 16);
	nvic_enable_irq(NVIC_FDCAN1_INTR1_IRQ);

	// nbus_init(&nbus, &can1.iface);
	// nbus_root_init(&root_channel, &nbus, UNIQUE_ID_REG, UNIQUE_ID_REG_LEN);
	// nbus_log_init(&log_channel, "log", &root_channel.channel);

	/*************** front panel CAN2 ******************************************/
	fdcan_init(CAN2, FDCAN_CCCR_INIT_TIMEOUT);
	fdcan_set_can(CAN2, true, false, true, false, 1, 8, 5, (16e6 / (CAN2_BITRATE) / 16) - 1);
	fdcan_set_fdcan(CAN2, true, true, 1, 8, 5, (16e6 / (CAN2_BITRATE) / 16) - 1);

	FDCAN_IE(CAN2) |= FDCAN_IE_RF0NE;
	FDCAN_ILE(CAN2) |= FDCAN_ILE_INT0;
	fdcan_start(CAN2, FDCAN_CCCR_INIT_TIMEOUT);
	stm32_fdcan_init(&can2, CAN2);
	nvic_set_priority(NVIC_FDCAN2_INTR0_IRQ, 6 * 16);
	nvic_enable_irq(NVIC_FDCAN2_INTR0_IRQ);

	nbus_switch_init(&switch1);
	nbus_switch_add_port(&switch1, &can1.iface);
	nbus_switch_add_port(&switch1, &can2.iface);
}


/* INTR0 and INTR1 swapped in libopencm3! */
void fdcan1_intr1_isr(void) {
	stm32_fdcan_irq_handler(&can1);
}

void fdcan2_intr0_isr(void) {
	stm32_fdcan_irq_handler(&can2);
}


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
	/* Backplane CAN1 port. Enable the transceiver permanently (SHDN = L). */
	gpio_mode_setup(CAN1_SHDN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, CAN1_SHDN_PIN);
	gpio_clear(CAN1_SHDN_PORT, CAN1_SHDN_PIN);
	gpio_mode_setup(CAN1_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, CAN1_RX_PIN);
	gpio_set_af(CAN1_RX_PORT, CAN1_AF, CAN1_RX_PIN);
	gpio_mode_setup(CAN1_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, CAN1_TX_PIN);
	gpio_set_af(CAN1_TX_PORT, CAN1_AF, CAN1_TX_PIN);

	/* CAN2 */
	gpio_mode_setup(CAN2_SHDN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, CAN2_SHDN_PIN);
	gpio_clear(CAN2_SHDN_PORT, CAN2_SHDN_PIN);
	gpio_mode_setup(CAN2_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, CAN2_RX_PIN);
	gpio_set_af(CAN2_RX_PORT, CAN2_AF, CAN2_RX_PIN);
	gpio_mode_setup(CAN2_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, CAN2_TX_PIN);
	gpio_set_af(CAN2_TX_PORT, CAN2_AF, CAN2_TX_PIN);

	/* LEDs */
	gpio_mode_setup(LED_WH_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_WH_PIN);
	gpio_mode_setup(LED_GN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_GN_PIN);
	gpio_mode_setup(LED_RD_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, LED_RD_PIN);
}


int32_t port_init(void) {
	port_setup_default_gpio();
	can_init();
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



