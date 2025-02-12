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

/* High level drivers */
#include <services/spi-flash/spi-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>
#include <services/i2c-eeprom/i2c-eeprom.h>
#include <services/stm32-clock/stm32-clock.h>

#if !defined(CONFIG_APP_BL)
	#include <services/generic-power/generic-power.h>
	#include <services/nbus2/nbus2.h>

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
 * NBUS2 init
 **********************************************************************************************************************/
Stm32Uart nbus_uart;
static void nbus_port_init(void) {
	/* USART2 RX/TX */
	gpio_mode_setup(GPIOD, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO5 | GPIO6);
	gpio_set_output_options(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO5 | GPIO6);
	gpio_set_af(GPIOD, GPIO_AF7, GPIO5 | GPIO6);

	/* Driver enable */
	gpio_mode_setup(GPIOD, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO4);
	gpio_clear(GPIOD, GPIO4);

	rcc_periph_clock_enable(RCC_USART2);

	stm32_uart_init(&nbus_uart, USART2);
	stm32_uart_set_rto(&nbus_uart, true);
	nbus_uart.uart.vmt->set_bitrate(&nbus_uart.uart, 1000000);

	nvic_enable_irq(NVIC_USART2_IRQ);
	nvic_set_priority(NVIC_USART2_IRQ, 5 * 16);

	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&(nbus_uart.stream), "nbus-uart");
}

void usart2_isr(void) {
	stm32_uart_interrupt_handler(&nbus_uart);
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
	/* LED1A */
	gpio_mode_setup(GPIOE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO10);
	gpio_clear(GPIOE, GPIO10);

	gpio_mode_setup(GPIOE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO11);
	gpio_clear(GPIOE, GPIO11);

	gpio_mode_setup(GPIOE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
	gpio_clear(GPIOE, GPIO12);

	gpio_mode_setup(GPIOE, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13);
	gpio_clear(GPIOE, GPIO13);

}


uint8_t packet_buffer[1024];
Nbus nbus;
int32_t port_init(void) {
	port_setup_default_gpio();

	#if !defined(CONFIG_APP_BL)
		stm32_clock_init(&cmgr, STM32_CLOCK_LEVEL_MEDIUM_PERF);
		stm32_clock_wait_init_done(&cmgr);
	#endif


	#if !defined(CONFIG_APP_BL)
		nbus_port_init();
		nbus_init(&nbus, &nbus_uart.stream);
		nbus_set_mac_key(&nbus, (uint8_t *)"abcd", 4);

		struct nbus_socket *socket = nbus_socket_allocate(&nbus);
		while (true) {
///*
			uint8_t local_id[] = {0x00, 0x00, 0x00, 0x25};
			nbus_socket_bind(socket, local_id, 1);

			struct datagram_msg rxmsg = {0};
			size_t len = sizeof(packet_buffer);
			if (socket->datagram.vmt->read(&socket->datagram, packet_buffer, &len, &rxmsg) == DATAGRAM_RET_OK) {

				struct datagram_msg txmsg = {
					.addr_size = rxmsg.addr_size,
					.dst_port = rxmsg.src_port,
				};
				memcpy(txmsg.dst_addr, rxmsg.src_addr, 4);
				socket->datagram.vmt->write(&socket->datagram, packet_buffer, len, &txmsg);
			}
//*/
/*
			uint8_t local_id[] = {0x00, 0x00, 0x00, 0x37};
			nbus_socket_bind(socket, local_id, 1);
			struct datagram_msg txmsg = {
				.addr_size = 4,
				.dst_port = 1,
			};
			memcpy(txmsg.dst_addr, (uint8_t[4]){0x22, 0x33, 0x44, 0x55}, 4);
			socket->datagram.vmt->write(&socket->datagram, packet_buffer, 32, &txmsg);
			vTaskDelay(100);
*/
		}

		nbus_socket_release(&nbus, socket);
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



