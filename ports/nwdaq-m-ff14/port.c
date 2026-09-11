/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-m-ff14 4-channel resistive force sensor digitizer
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <stm32g4xx.h>

#include <main.h>
#include "port.h"

#include <interfaces/servicelocator.h>
#include <interfaces/sensor.h>

/* Low level drivers for the STM32G4 family */
#include <services/stm32-clock/stm32-clock.h>
#include <services/stm32-gpio/stm32-gpio.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/adc-mcp3564/mcp3564.h>
#include <services/uart-ow/uart-ow.h>
#include <services/generic-mux/generic-mux.h>

#include <services/stm32-flash/stm32-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>
#if !defined(CONFIG_APP_BL)
	#include <services/flash-cbor-mib/flash-cbor-mib.h>
#endif

#define MODULE_NAME "port"


/**
 * Port specific global variables and singleton instances.
 */

uint32_t SystemCoreClock;
Stm32Clock cmgr;

Stm32Gpio gpioa;
Stm32Gpio gpiob;
Stm32Gpio gpioc;


int32_t port_early_init(void) {
	SystemCoreClock = 16e6;

	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
	RCC->AHB2ENR |= RCC_AHB2ENR_DAC1EN;

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

Stm32Uart uart2;
static void port_setup_console(void) {
	RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;

	/* USART2 TX/RX on PA2/PA3, AF7. */
	gpioa.pin[2].vmt->set_mode(&(gpioa.pin[2]), MODE_ALTERNATE);
	gpioa.pin[2].vmt->set_pull(&(gpioa.pin[2]), PULL_UP);
	gpioa.pin[2].vmt->set_pinmux(&(gpioa.pin[2]), 7);
	gpioa.pin[3].vmt->set_mode(&(gpioa.pin[3]), MODE_ALTERNATE);
	gpioa.pin[3].vmt->set_pull(&(gpioa.pin[3]), PULL_UP);
	gpioa.pin[3].vmt->set_pinmux(&(gpioa.pin[3]), 7);

	/* Initialise and configure the UART */
	stm32_uart_init(&uart2, (void *)USART2);
	uart2.uart.vmt->set_bitrate(&uart2.uart, 115200);

	/* TX/RX are crossed on the board, use the USART internal pin swap so TX drives PA3 and RX
	 * listens on PA2. */
	stm32_uart_set_rxtx_swap(&uart2, true);

	NVIC_EnableIRQ(USART2_IRQn);
	NVIC_SetPriority(USART2_IRQn, 7);

	/* Advertise the console stream output and set it as default for log output. */
	Stream *console = &uart2.stream;
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)console, "console");
	u_log_set_stream(console);
}


void usart2_isr(void);
void usart2_isr(void) {
	stm32_uart_interrupt_handler(&uart2);
}


/**********************************************************************************************************************
 * nbus2 backplane stream on USART1
 **********************************************************************************************************************/

/* USART1 carries the nbus2 protocol on the backplane. The port only sets up the USART and advertises
 * its byte stream; the application discovers the stream and builds the nbus2 stack (framing, MAC and
 * the flash/conf protocols) on top of it. */
Stm32Uart uart1;

Gpio *nbus2_shdn_gpio = &(gpioa.pin[1]);

static void port_setup_nbus2(void) {
	/* The nbus2 transceiver shutdown on PA1 is active high; drive it low to enable the transceiver. */
	nbus2_shdn_gpio->vmt->set_mode(nbus2_shdn_gpio, MODE_OUTPUT);
	nbus2_shdn_gpio->vmt->set(nbus2_shdn_gpio, false);

	/* USART1 TX/RX on PB6/PB7, AF7. */
	gpiob.pin[6].vmt->set_mode(&(gpiob.pin[6]), MODE_ALTERNATE);
	gpiob.pin[6].vmt->set_pinmux(&(gpiob.pin[6]), 7);
	gpiob.pin[7].vmt->set_mode(&(gpiob.pin[7]), MODE_ALTERNATE);
	gpiob.pin[7].vmt->set_pinmux(&(gpiob.pin[7]), 7);

	stm32_uart_init(&uart1, (void *)USART1);
	//stm32_uart_set_rto(&uart1, true);
	uart1.uart.vmt->set_bitrate(&uart1.uart, 1000000);

	NVIC_EnableIRQ(USART1_IRQn);
	NVIC_SetPriority(USART1_IRQn, 7);

	/* Advertise the raw backplane stream. The application frames nbus2 datagrams onto it. */
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&uart1.stream, "nbus");
}


void usart1_isr(void);
void usart1_isr(void) {
	stm32_uart_interrupt_handler(&uart1);
}


/**********************************************************************************************************************
 * 1-Wire bus master and input multiplexer on USART3
 **********************************************************************************************************************/

/* USART3 drives a 1-Wire bus (via uart-ow). It is wired full-duplex with TX on PB10 and RX on PC11 through an
 * external transceiver; the TX signal is inverted (idles low) so it drives the transceiver's open-drain output. Two
 * GPIO select lines feed a generic mux that steers the 1-Wire bus to one of the physical connectors. */
Stm32Uart uart3;
UartOw uart_ow;

Gpio *ow_enable_gpio = &(gpiob.pin[14]);
Gpio *ow_mux0_gpio = &(gpiob.pin[13]);
Gpio *ow_mux1_gpio = &(gpiob.pin[12]);

GenericMux ow_mux;
static struct generic_mux_sel_line ow_mux_lines[2];

static void port_setup_ow(void) {
	RCC->APB1ENR1 |= RCC_APB1ENR1_USART3EN;

	/* USART3 TX on PB10, AF7 (inverted, idles low). */
	gpiob.pin[10].vmt->set_mode(&(gpiob.pin[10]), MODE_ALTERNATE);
	gpiob.pin[10].vmt->set_pinmux(&(gpiob.pin[10]), 7);
	/* USART3 RX on PC11, AF7. */
	gpioc.pin[11].vmt->set_mode(&(gpioc.pin[11]), MODE_ALTERNATE);
	gpioc.pin[11].vmt->set_pinmux(&(gpioc.pin[11]), 7);

	/* Full duplex: TX and RX use separate pins, so no single-wire half-duplex mode. */
	stm32_uart_init(&uart3, (void *)USART3);
	stm32_uart_set_tx_invert(&uart3, true);
	/* Two stop bits extend the high tail of every bit slot by one bit time to help recharge the weakly driven bus. */
	uart3.uart.vmt->set_stopbits(&uart3.uart, UART_STOPBITS_2);

	NVIC_EnableIRQ(USART3_IRQn);
	NVIC_SetPriority(USART3_IRQn, 7);

	/* 1-Wire bus master over USART3. Advertise the Ow interface as the TEDS bus. */
	uart_ow_init(&uart_ow, &uart3.uart, &uart3.stream);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_OW, (Interface *)&uart_ow.ow, "teds");

	/* 1-Wire mux select lines: mux0 on PB13, mux1 on PB12. */
	ow_mux0_gpio->vmt->set_mode(ow_mux0_gpio, MODE_OUTPUT);
	ow_mux0_gpio->vmt->set(ow_mux0_gpio, false);
	ow_mux1_gpio->vmt->set_mode(ow_mux1_gpio, MODE_OUTPUT);
	ow_mux1_gpio->vmt->set(ow_mux1_gpio, false);

	ow_mux_lines[0].gpio = ow_mux0_gpio;
	ow_mux_lines[1].gpio = ow_mux1_gpio;
	generic_mux_init(&ow_mux, NULL, &ow_mux_lines, 2);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_MUX, (Interface *)&ow_mux.mux, "teds-mux");

	/* Enable the 1-Wire bus voltage on PB14 and steer the mux to the first bus. */
	ow_enable_gpio->vmt->set_mode(ow_enable_gpio, MODE_OUTPUT);
	ow_enable_gpio->vmt->set(ow_enable_gpio, true);
	ow_mux.mux.vmt->select(&ow_mux.mux, 0);

	/* It needs some time. */
	vTaskDelay(10);
}


void usart3_isr(void);
void usart3_isr(void) {
	stm32_uart_interrupt_handler(&uart3);
}


void vPortSetupTimerInterrupt(void);
void vPortSetupTimerInterrupt(void) {
	/* Initialize systick interrupt for FreeRTOS. */
	NVIC_SetPriority(SysTick_IRQn, 15);
	SysTick->LOAD = 16000UL - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}


static void port_setup_default_gpio(void) {
	/* VREF_POL on PA10, driven low by default. */
	gpioa.pin[10].vmt->set_mode(&(gpioa.pin[10]), MODE_OUTPUT);
	gpioa.pin[10].vmt->set(&(gpioa.pin[10]), false);
}


/**********************************************************************************************************************
 * Sensor excitation using the DAC
 **********************************************************************************************************************/

static void port_setup_dac(void) {
	/* Excitation outputs DAC1_OUT1/DAC1_OUT2 on PA4/PA5. */
	gpioa.pin[4].vmt->set_mode(&(gpioa.pin[4]), MODE_ANALOG);
	gpioa.pin[5].vmt->set_mode(&(gpioa.pin[5]), MODE_ANALOG);

	/* Enable excitation. The two DAC channels output static levels of 0.1 and 0.9
	 * of the full scale (12 bit right-aligned). */
	DAC1->DHR12R1 = (uint16_t)(0.1f * 4095.0f);
	DAC1->DHR12R2 = (uint16_t)(0.9f * 4095.0f);
	DAC1->CR |= DAC_CR_EN1;
	DAC1->CR |= DAC_CR_EN2;
}


/**********************************************************************************************************************
 * MCP3564 ADC readout and per-channel Sensor interfaces
 **********************************************************************************************************************/

Stm32SpiBus spi1;
Stm32SpiDev spi1_adc;
Mcp3564 mcp;

/* A single differential force-sensor input exported as a Sensor interface. Reading it points the ADC
 * multiplexer at this channel's differential input pair, triggers a single conversion and returns the
 * fresh result. There is no background task or timing: everything happens synchronously in value_f. */
typedef struct {
	Sensor sensor;
	enum mcp3564_mux muxp;
	enum mcp3564_mux muxm;
} Ff14AdcSensor;

static sensor_ret_t ff14_adc_sensor_value_f(Sensor *sensor, float *value) {
	Ff14AdcSensor *self = sensor->parent;

	/* Point the multiplexer at this channel's differential pair and start a single conversion. */
	mcp3564_set_mux(&mcp, self->muxp, self->muxm);
	mcp3564_update(&mcp);
	vTaskDelay(1);
	mcp3564_send_cmd(&mcp, MCP3564_CMD_START, NULL, NULL, 0, NULL, 0);

	/* The ADC is configured for the 32 bit right-justified signed data format. */
	int32_t raw = 0;
	uint8_t status = 0x04;
	for (int i = 0; (status & 0x04) && i < 100; i++) {
		vTaskDelay(1);
		mcp3564_read_reg(&mcp, MCP3564_REG_ADCDATA, 4, (uint32_t *)&raw, &status);
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("status = 0x%02x"), status);
	}

	if (value != NULL) {
		*value = (float)raw;
	}
	return SENSOR_RET_OK;
}

static const struct sensor_vmt ff14_adc_sensor_vmt = {
	.value_f = ff14_adc_sensor_value_f,
};

static const struct sensor_info ff14_adc_sensor_info = {
	.description = "differential force sensor input",
	.unit = "LSB",
};

Ff14AdcSensor adc_sensors[4];

static void port_setup_adc(void) {
	/* SPI1 SCK/MISO/MOSI on PB3/PB4/PB5, AF5. */
	gpiob.pin[3].vmt->set_mode(&(gpiob.pin[3]), MODE_ALTERNATE);
	gpiob.pin[3].vmt->set_pinmux(&(gpiob.pin[3]), 5);
	gpiob.pin[4].vmt->set_mode(&(gpiob.pin[4]), MODE_ALTERNATE);
	gpiob.pin[4].vmt->set_pinmux(&(gpiob.pin[4]), 5);
	gpiob.pin[5].vmt->set_mode(&(gpiob.pin[5]), MODE_ALTERNATE);
	gpiob.pin[5].vmt->set_pinmux(&(gpiob.pin[5]), 5);

	stm32_spibus_init(&spi1, (void *)SPI1, STM32_SPI_PER_TYPE_SPI);
	spi1.bus.vmt->set_sck_freq(&spi1.bus, 4e6);
	spi1.bus.vmt->set_mode(&spi1.bus, 0, 0);

	/* Chip select on PA12, configured and driven by the SPI device driver. */
	stm32_spidev_init(&spi1_adc, &spi1.bus, &(gpioa.pin[12]));

	mcp3564_init(&mcp, &(spi1_adc.dev));
	mcp3564_set_stp_enable(&mcp, false);
	mcp3564_set_gain(&mcp, MCP3564_GAIN_1);
	mcp3564_set_osr(&mcp, MCP3564_OSR_8192);
	mcp3564_set_mux(&mcp, MCP3564_MUX_VCM, MCP3564_MUX_CH4);
	mcp3564_set_irq_mode(&mcp, MCP3564_IRQ_MODE_IRQ);
	mcp3564_set_stp_enable(&mcp, true);
	mcp3564_update(&mcp);

	/* Four differential inputs: AIN0/AIN1, AIN2/AIN3, AIN4/AIN5, AIN6/AIN7. Each becomes a Sensor. */
	static const struct {
		enum mcp3564_mux muxp;
		enum mcp3564_mux muxm;
		const char *name;
	} channels[4] = {
		{MCP3564_MUX_CH0, MCP3564_MUX_CH1, "ch0"},
		{MCP3564_MUX_CH2, MCP3564_MUX_CH3, "ch1"},
		{MCP3564_MUX_CH4, MCP3564_MUX_CH5, "ch2"},
		{MCP3564_MUX_CH6, MCP3564_MUX_CH7, "ch3"},
	};
	for (size_t i = 0; i < 4; i++) {
		adc_sensors[i].muxp = channels[i].muxp;
		adc_sensors[i].muxm = channels[i].muxm;
		adc_sensors[i].sensor.vmt = &ff14_adc_sensor_vmt;
		adc_sensors[i].sensor.info = &ff14_adc_sensor_info;
		adc_sensors[i].sensor.parent = &(adc_sensors[i]);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&adc_sensors[i].sensor, channels[i].name);
	}
}


/**********************************************************************************************************************
 * Internal flash memory, MIB and nbus-flash access
 **********************************************************************************************************************/

Stm32Flash iflash;
FlashVolStatic pv_iflash;
Flash *lv_bl;
Flash *lv_blconf;
Flash *lv_mib;
Flash *lv_app;
Flash *lv_conf;
Flash *lv_update;
Flash *lv_backup;
#if !defined(CONFIG_APP_BL)
FlashCborMib mib;
#endif

static void port_setup_flash(void) {
	stm32_flash_init(&iflash);

	flash_vol_static_init(&pv_iflash, &iflash.flash);
	flash_vol_static_create(&pv_iflash, "bootloader", 0,          60 * 1024,  &lv_bl);
	flash_vol_static_create(&pv_iflash, "bootconf",   60 * 1024,  2 * 1024,   &lv_blconf);
	flash_vol_static_create(&pv_iflash, "mib",        62 * 1024,  2 * 1024,   &lv_mib);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_mib, "mib");
	flash_vol_static_create(&pv_iflash, "app",        64 * 1024,  184 * 1024, &lv_app);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_app, "app");
	flash_vol_static_create(&pv_iflash, "conf",       248 * 1024, 8 * 1024,   &lv_conf);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_conf, "conf");
	flash_vol_static_create(&pv_iflash, "update",     256 * 1024, 128 * 1024, &lv_update);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_update, "update");
	flash_vol_static_create(&pv_iflash, "backup",     384 * 1024, 128 * 1024, &lv_backup);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_backup, "backup");

	#if !defined(CONFIG_APP_BL)
		const struct flash_cbor_mib_conf mib_conf = {
			.flash = lv_mib,
			.offset = 0,
			.max_size = 0,
			.public_key_b64 = CONFIG_PORT_NWDAQ_M_FF14_MIB_PUBKEY,
			.root_name = "mib",
		};
		if (flash_cbor_mib_init(&mib, &mib_conf) == FLASH_CBOR_MIB_RET_OK) {
			Conf *mib_root = NULL;
			flash_cbor_mib_get_root(&mib, &mib_root);
			iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CONF, (Interface *)mib_root, "mib");
		}
	#endif
}


int32_t port_init(void) {
	stm32_gpio_init(&gpioa, (void *)0x48000000);
	stm32_gpio_init(&gpiob, (void *)0x48000400);
	stm32_gpio_init(&gpioc, (void *)0x48000800);

	port_setup_console();
	port_setup_default_gpio();

	stm32_clock_init(&cmgr, STM32_CLOCK_LEVEL_MEDIUM_PERF);
	stm32_clock_wait_init_done(&cmgr);
	SystemCoreClock = 128e6;

	#if !defined(CONFIG_APP_BL)
		port_setup_nbus2();
	#endif

	port_setup_flash();

	#if !defined(CONFIG_APP_BL)
		port_setup_dac();
		port_setup_adc();
		port_setup_ow();
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
