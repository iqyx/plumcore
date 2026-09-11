/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-m-gnss2 GNSS, L-band and UHF receiver
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

#include <stm32g4xx.h>

#include <main.h>
#include "port.h"

#include <interfaces/sensor.h>
#include <interfaces/servicelocator.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>
#include <interfaces/flash.h>

/* Low level drivers for the STM32G4 family */
#include <services/stm32-gpio/stm32-gpio.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-watchdog/watchdog.h>
#include <services/stm32-clock/stm32-clock.h>

/* High level drivers */
#include <services/stm32-flash/stm32-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>

#if !defined(CONFIG_APP_BL)
	#include <services/nbus2/nbus2.h>
	#include <services/proto-dgstream/proto-dgstream.h>
	#include <services/nbus-flash/nbus-flash.h>
	#include <services/proto-stream/proto-stream.h>
	#include <services/gps-ublox/gps-ublox.h>
#endif

#define MODULE_NAME "port"


/**
 * Port specific global variables and singleton instances.
 */

uint32_t SystemCoreClock;

Watchdog watchdog;

#if !defined(CONFIG_APP_BL)
	Stm32Clock cmgr;
	Stm32Gpio gpioa;
	Stm32Gpio gpiob;
	Stm32Gpio gpioc;

	Gpio *gpio_x20p_power_en = &gpiob.pin[0];
	Gpio *gpio_x20p_safeboot = &gpiob.pin[1];
	Gpio *gpio_d9s_power_en = &gpioc.pin[13];
	Gpio *gpio_d9s_safeboot = &gpiob.pin[5];

	Gpio *gpio_usart1_tx = &gpiob.pin[6];
	Gpio *gpio_usart1_rx = &gpiob.pin[7];
	Gpio *gpio_nbus2_shdn = &gpioa.pin[11];

	Gpio *gpio_x20p_txd1 = &gpioa.pin[2];
	Gpio *gpio_x20p_rxd1 = &gpioa.pin[3];

#endif


int32_t port_early_init(void) {
	SystemCoreClock = 16e6;

	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * Default startup GPIO configuration
 **********************************************************************************************************************/

static void port_setup_gpio(void) {
	stm32_gpio_init(&gpioa, (void *)GPIOA_BASE);
	stm32_gpio_init(&gpiob, (void *)GPIOB_BASE);
	stm32_gpio_init(&gpioc, (void *)GPIOC_BASE);

	gpio_x20p_power_en->vmt->set_mode(gpio_x20p_power_en, MODE_OUTPUT);
	gpio_x20p_power_en->vmt->set(gpio_x20p_power_en, false);

	gpio_x20p_safeboot->vmt->set_mode(gpio_x20p_safeboot, MODE_OUTPUT);
	gpio_x20p_safeboot->vmt->set(gpio_x20p_safeboot, true);

	gpio_d9s_power_en->vmt->set_mode(gpio_d9s_power_en, MODE_OUTPUT);
	gpio_d9s_power_en->vmt->set(gpio_d9s_power_en, false);

	gpio_d9s_safeboot->vmt->set_mode(gpio_d9s_safeboot, MODE_OUTPUT);
	gpio_d9s_safeboot->vmt->set(gpio_d9s_safeboot, true);
}


/**********************************************************************************************************************
 * nbus2 backplane port setup
 **********************************************************************************************************************/

Stm32Uart nbus2_uart;
ProtoDgstream nbus2_dgstream;
Nbus nbus;
static void port_setup_nbus2(void) {

	/* Configure the USART1 GPIO first. */
	gpio_usart1_tx->vmt->set_mode(gpio_usart1_tx, MODE_ALTERNATE);
	gpio_usart1_tx->vmt->set_pinmux(gpio_usart1_tx, 7);
	gpio_usart1_rx->vmt->set_mode(gpio_usart1_rx, MODE_ALTERNATE);
	gpio_usart1_rx->vmt->set_pinmux(gpio_usart1_rx, 7);

	/* Enable the transceiver. */
	gpio_nbus2_shdn->vmt->set_mode(gpio_nbus2_shdn, MODE_OUTPUT);
	gpio_nbus2_shdn->vmt->set(gpio_nbus2_shdn, false);

	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

	stm32_uart_init(&nbus2_uart, (void *)USART1);
	stm32_uart_set_rto(&nbus2_uart, true);
	nbus2_uart.uart.vmt->set_bitrate(&nbus2_uart.uart, 2e6);

	NVIC_EnableIRQ(USART1_IRQn);
	NVIC_SetPriority(USART1_IRQn, 7);

	/* Frame nbus2 packets onto the USART1 byte stream and run nbus2 on top of the resulting Datagram
	 * interface. The proto-dgstream service handles medium access (framing and the inter-frame gaps);
	 * nbus2 only deals with the protocol itself. */
	Datagram *nbus_dgram = NULL;
	proto_dgstream_init(&nbus2_dgstream, &nbus2_uart.stream);
	proto_dgstream_set_rx_timeout(&nbus2_dgstream, 1);
	proto_dgstream_set_tx_gap(&nbus2_dgstream, 2);
	proto_dgstream_get_datagram(&nbus2_dgstream, &nbus_dgram);

	const struct nbus_config nbus_config = {
		.dgram = nbus_dgram,
		.tx_crypto = NBUS_CRYPTO_CHACHA20_HALFSIPHASH,
		.rx_crypto = NBUS_CRYPTO_BLAKE2S_SIV | NBUS_CRYPTO_CHACHA20_HALFSIPHASH,
	};
	nbus_init(&nbus, &nbus_config);
	nbus_set_mac_key(&nbus, (uint8_t *)"abcd", 4);
	//iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&(nbus2_uart.stream), "nbus2_stream");
}

void usart1_isr(void) {
	stm32_uart_interrupt_handler(&nbus2_uart);
}


/**********************************************************************************************************************
 * Setup logging over a virtual stream interface
 **********************************************************************************************************************/

ProtoStream proto_stream_log;
struct nbus_socket *proto_stream_log_socket;

static void port_setup_logging(void) {
	const uint8_t local_ep = 1;
	uint8_t local_id[4] = {0x00, 0x00, 0x00, 0x31};

	proto_stream_log_socket = nbus_socket_allocate(&nbus);
	nbus_socket_bind(proto_stream_log_socket, local_id, local_ep);
	proto_stream_init(&proto_stream_log, &proto_stream_log_socket->datagram, 256, 2048);

	u_log_set_stream(&(proto_stream_log.stream));
}


/**********************************************************************************************************************
 * Flash memory partition setup
 **********************************************************************************************************************/

Stm32Flash iflash;
FlashVolStatic pv_iflash;

Flash *lv_bl;
Flash *lv_conf;
Flash *lv_mib;
Flash *lv_app;
struct nbus_socket *flash_proto_socket;
NbusFlash flash_proto;

static void port_setup_flash(void) {
	stm32_flash_init(&iflash);

	flash_vol_static_init(&pv_iflash, &iflash.flash);
	flash_vol_static_create(&pv_iflash, "bootloader", 0,          60 * 1024,  &lv_bl);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_bl, "bootloader");

	flash_vol_static_create(&pv_iflash, "bootconf",   60 * 1024,  2 * 1024,   &lv_conf);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_conf, "bootconf");

	flash_vol_static_create(&pv_iflash, "mib",        62 * 1024,  2 * 1024,   &lv_mib);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_mib, "mib");

	flash_vol_static_create(&pv_iflash, "app",        64 * 1024,  448 * 1024, &lv_app);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_app, "app");

	/* Initialize nbus2-flash protocol on a known SID (temporary). */
	const uint8_t local_ep = 1;
	uint8_t local_id[4] = {0x00, 0x00, 0x00, 0x30};

	flash_proto_socket = nbus_socket_allocate(&nbus);
	nbus_socket_bind(flash_proto_socket, local_id, local_ep);
	nbus_flash_init(&flash_proto, &flash_proto_socket->datagram);

}


/**********************************************************************************************************************
 * GNSS init
 **********************************************************************************************************************/

Stm32Uart x20p_uart1;
GpsUblox x20p;
ProtoStream gnss_nmea_proto_stream;
struct nbus_socket *gnss_nmea_socket;
ProtoStream gnss_ubx_proto_stream;
struct nbus_socket *gnss_ubx_socket;
ProtoStream gnss_rtcm_proto_stream;
struct nbus_socket *gnss_rtcm_socket;

static void port_setup_x20p(void) {

	/* Configure the USART2 GPIO first. */
	gpio_x20p_txd1->vmt->set_mode(gpio_x20p_txd1, MODE_ALTERNATE);
	gpio_x20p_txd1->vmt->set_pinmux(gpio_x20p_txd1, 7);
	gpio_x20p_rxd1->vmt->set_mode(gpio_x20p_rxd1, MODE_ALTERNATE);
	gpio_x20p_rxd1->vmt->set_pinmux(gpio_x20p_rxd1, 7);

	RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;

	stm32_uart_init(&x20p_uart1, (void *)USART2);
	x20p_uart1.uart.vmt->set_bitrate(&x20p_uart1.uart, 115200);

	NVIC_EnableIRQ(USART2_IRQn);
	NVIC_SetPriority(USART2_IRQn, 7);

	/* Power on the GNSS receiver with safeboot at H. */
	gpio_x20p_power_en->vmt->set(gpio_x20p_power_en, true);
	vTaskDelay(2000);
	gps_ublox_init(&x20p);
	gps_ublox_set_uart_transport(&x20p, &(x20p_uart1.stream), &(x20p_uart1.uart));
	gps_ublox_start(&x20p);
	vTaskDelay(1000);

	gnss_nmea_socket = nbus_socket_allocate(&nbus);
	if (gnss_nmea_socket != NULL) {
		nbus_socket_bind(gnss_nmea_socket, (uint8_t []){0x00, 0x00, 0x00, 0x32}, 1);
		proto_stream_init(&gnss_nmea_proto_stream, &gnss_nmea_socket->datagram, 16, 2048);
		gps_ublox_set_nmea_out_stream(&x20p, &(gnss_nmea_proto_stream.stream));
	}

	gnss_ubx_socket = nbus_socket_allocate(&nbus);
	if (gnss_ubx_socket != NULL) {
		nbus_socket_bind(gnss_ubx_socket, (uint8_t []){0x00, 0x00, 0x00, 0x33}, 1);
		proto_stream_init(&gnss_ubx_proto_stream, &gnss_ubx_socket->datagram, 16, 2048);
		gps_ublox_set_ubx_out_stream(&x20p, &(gnss_ubx_proto_stream.stream));
	}

	gnss_rtcm_socket = nbus_socket_allocate(&nbus);
	if (gnss_rtcm_socket != NULL) {
		nbus_socket_bind(gnss_rtcm_socket, (uint8_t []){0x00, 0x00, 0x00, 0x34}, 1);
		proto_stream_init(&gnss_rtcm_proto_stream, &gnss_rtcm_socket->datagram, 2048, 16);
		gps_ublox_set_rtcm_in_stream(&x20p, &(gnss_rtcm_proto_stream.stream));
	}
}

void usart2_isr(void) {
	stm32_uart_interrupt_handler(&x20p_uart1);
}


/**********************************************************************************************************************
 * Port init
 **********************************************************************************************************************/

void vPortSetupTimerInterrupt(void);
void vPortSetupTimerInterrupt(void) {
	/* Initialize systick interrupt for FreeRTOS. SysTick is clocked from HCLK
	 * (SystemCoreClock), so derive the reload from the configured tick rate. */
	NVIC_SetPriority(SysTick_IRQn, 15);
	//SysTick->LOAD = (SystemCoreClock / configTICK_RATE_HZ) - 1;
	SysTick->LOAD = (16e6 / configTICK_RATE_HZ) - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

//struct nbus_socket *socket;

int32_t port_init(void) {
	watchdog_init(&watchdog, 8000, 1);
	port_setup_gpio();

	#if !defined(CONFIG_APP_BL)
		stm32_clock_init(&cmgr, STM32_CLOCK_LEVEL_MEDIUM_PERF);
		stm32_clock_wait_init_done(&cmgr);
		SystemCoreClock = 128e6;
	#endif

	port_setup_nbus2();
	port_setup_logging();
	port_setup_flash();
	port_setup_x20p();

	/* Power on the L-band receiver. */
	gpio_d9s_power_en->vmt->set(gpio_d9s_power_en, true);

	#if !defined(CONFIG_APP_BL)



	#endif

	return PORT_INIT_OK;
}


/* Configure dedicated timer (TIM6) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {
	RCC->APB1RSTR1 |= RCC_APB1RSTR1_TIM6RST;
	RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_TIM6RST;
	/* The timer should run at 1MHz */
	TIM6->PSC = 16 - 1;
	TIM6->CR1 &= ~TIM_CR1_OPM;
	TIM6->ARR = UINT16_MAX;
	TIM6->CR1 |= TIM_CR1_CEN;
}


uint32_t port_task_timer_get_value(void) {
	return TIM6->CNT;
}
