/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nwdaq-br28-fdc port-specific configuration
 *
 * Copyright (c) 2022-2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

/**
 * @todo
 *
 * - tidy up port.c and document stuff
 * - remove filesystem and fifo service init, they should not be here. Move to the application code instead, they
 *   are not hardware defined and can be easily replaced depending on the application.
 * - refactor LED service and initialise LEDs properly
 * - HW update request: measure the input voltage and current consumption, which should help in deciding when to
 *   schedule measurements
 * - a proper file transfer service without the use of CLI. Define an API with reliable transport over a Stream
 *   interface, define basic functions for file manipulation, filesystem manipulation, file transfer.
 *   Run the api as a CLI command, hence: allow arbitrary text (user presses random keys when the API command
 *   is started), user wants to end the API command (accept some common break sequence, ctrl+c maybe?).
 *   Regain command framing when it is lost (magic numbers?)
 *
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

#include "interface_flash.h"
#include "module_spi_flash.h"

#include <interfaces/sensor.h>
#include <interfaces/servicelocator.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>
#include <interfaces/adc.h>

#include <services/cli/system_cli_tree.h>

/* Low level drivers for th STM32G4 family */
#include <services/stm32-system-clock/clock.h>
#include <services/stm32-rtc/rtc.h>
#include <services/stm32-i2c/stm32-i2c.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-adc/stm32-adc.h>
#include <services/stm32-watchdog/watchdog.h>
#include <services/stm32-dac/stm32-dac.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/stm32-fdcan/stm32-fdcan.h>

/* High level drivers */
#include <services/adc-mcp3564/mcp3564.h>
#include <services/adc-composite/adc-composite.h>
#include <services/generic-power/generic-power.h>
#include <services/generic-mux/generic-mux.h>
#include <services/spi-flash/spi-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>
#include <services/fs-spiffs/fs-spiffs.h>
#include <services/flash-fifo/flash-fifo.h>
#include <services/adc-sensor/adc-sensor.h>
#include <services/nbus/nbus.h>
#include <services/nbus/nbus-root.h>
#include <services/nbus/nbus-log.h>
#include <services/nbus-mq/nbus-mq.h>
#include <services/i2c-eeprom/i2c-eeprom.h>

/* Applets */
#include <applets/hello-world/hello-world.h>
#include <applets/tempco-calibration/tempco-cal.h>

/**
 * Port specific global variables and singleton instances.
 */

Watchdog watchdog;
Stm32Rtc rtc;
/* This is somewhat mandatory as the main purpose of the device is to measure something. */
Mcp3564 mcp;
Stm32Dac dac1_1;
Stm32Dac dac1_2;
GenericPower exc_power;
GenericMux input_mux;
Stm32SpiBus spi2;
Stm32SpiDev spi2_adc;


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

	/* ADC is used for PCB temperature measurement */
	rcc_periph_clock_enable(RCC_ADC1);
	/** @todo needed fo G4? */
	RCC_CCIPR |= 3 << 28;

	/* FDCAN interface */
	rcc_periph_clock_enable(RCC_FDCAN);
	rcc_periph_clock_enable(SCC_FDCAN);
	/* Set PCLK as FDCAN clock */
	RCC_CCIPR |= (RCC_CCIPR_FDCAN_PCLK << RCC_CCIPR_FDCAN_SHIFT);

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
		nvic_set_priority(NVIC_USART2_IRQ, 7 * 16);

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
		interface_led_loop(&led_status.iface, 0xff);
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


/* Generic GPIO mux for channel 0-3/4-7 selection *********************************************************************/
const struct generic_mux_sel_line input_mux_lines[] = {
	{.port = MUX_A0_PORT, .pin = MUX_A0_PIN},
	{.port = MUX_A1_PORT, .pin = MUX_A1_PIN},
};


/* Reference voltage positive/negative mux ****************************************************************************/
static mux_ret_t vref_mux_enable(Mux *self, bool enable) {
	(void)self;
	(void)enable;

	return MUX_RET_OK;
}


static mux_ret_t vref_mux_select(Mux *self, uint32_t channel) {
	(void)self;

	if (channel == 0) {
		/* Positive Vref mux. */
		gpio_set(VREF1_SEL_PORT, VREF1_SEL_PIN);
		gpio_set(VREF2_SEL_PORT, VREF2_SEL_PIN);
		return MUX_RET_OK;
	} else if (channel == 1) {
		gpio_clear(VREF1_SEL_PORT, VREF1_SEL_PIN);
		gpio_clear(VREF2_SEL_PORT, VREF2_SEL_PIN);
		return MUX_RET_OK;
	} else {
		return MUX_RET_FAILED;
	}
}


static const struct mux_vmt vref_mux_vmt = {
	.enable = vref_mux_enable,
	.select = vref_mux_select,
};

Mux vref_mux;


/* MCP3564 internal mux for selecting first/second half of inputs *****************************************************/
static mux_ret_t mcp_mux_enable(Mux *self, bool enable) {
	(void)self;
	(void)enable;

	return MUX_RET_OK;
}


static mux_ret_t mcp_mux_select(Mux *self, uint32_t channel) {
	(void)self;

	if (channel == 0) {
		mcp3564_set_mux(&mcp, MCP3564_MUX_CH0, MCP3564_MUX_CH1);
		mcp3564_update(&mcp);
		return MUX_RET_OK;
	} else if (channel == 1) {
		mcp3564_set_mux(&mcp, MCP3564_MUX_CH2, MCP3564_MUX_CH3);
		mcp3564_update(&mcp);
		return MUX_RET_OK;
	} else {
		return MUX_RET_FAILED;
	}
}


static const struct mux_vmt mcp_mux_vmt = {
	.enable = mcp_mux_enable,
	.select = mcp_mux_select,
};

Mux mcp_mux;


static void adc_init(void) {
	rcc_periph_clock_enable(RCC_SPI2);
	/* GPIO is already initialised */

	stm32_spibus_init(&spi2, SPI2);
	spi2.bus.vmt->set_sck_freq(&spi2.bus, 2e6);
	spi2.bus.vmt->set_mode(&spi2.bus, 0, 0);

	stm32_spidev_init(&spi2_adc, &spi2.bus, ADC_CS_PORT, ADC_CS_PIN);

	mcp3564_init(&mcp, &(spi2_adc.dev));
	mcp3564_set_stp_enable(&mcp, false);
	mcp3564_set_gain(&mcp, MCP3564_GAIN_2);
	mcp3564_set_osr(&mcp, MCP3564_OSR_81920);
	mcp3564_set_mux(&mcp, MCP3564_MUX_CH0, MCP3564_MUX_CH1);
	mcp3564_update(&mcp);

	generic_mux_init(&input_mux, MUX_EN_PORT, MUX_EN_PIN, &input_mux_lines, 2);

	/* Vref mux init */
	vref_mux.parent = NULL;
	vref_mux.vmt = &vref_mux_vmt;

	/* MCP3564 mux init */
	mcp_mux.parent = NULL;
	mcp_mux.vmt = &mcp_mux_vmt;
}

/**********************************************************************************************************************
 * Sensor excitation initialisation
 **********************************************************************************************************************/

static void exc_init(void) {
	/* Excitation is handled as a generic power device which can be enabled/disabled, it's voltage/polarity set. */
	generic_power_init(&exc_power);
	generic_power_set_vref(&exc_power, 3.3f);

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

/**********************************************************************************************************************
 * Flash memory initialisation
 **********************************************************************************************************************/

Stm32SpiBus spi3;
Stm32SpiDev spi3_flash;
SpiFlash nor_flash;

FlashVolStatic lvs;
Flash *lv_boot;
Flash *lv_system;
Flash *lv_test;

FsSpiffs spiffs_system;
FlashFifo fifo;

static void nor_flash_init(void) {
	/* SPI for the NOR flash is not initialised in the default GPIO setup. Do full setup here. */
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO3 | GPIO4 | GPIO5);
	gpio_set_af(GPIOB, GPIO_AF6, GPIO3 | GPIO4 | GPIO5);
	/* FLASH_CS pin */
	gpio_set(FLASH_CS_PORT, FLASH_CS_PIN);
	gpio_mode_setup(FLASH_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, FLASH_CS_PIN);

	rcc_periph_clock_enable(RCC_SPI3);
	stm32_spibus_init(&spi3, SPI3);
	spi3.bus.vmt->set_sck_freq(&spi3.bus, 10e6);
	spi3.bus.vmt->set_mode(&spi3.bus, 0, 0);
	stm32_spidev_init(&spi3_flash, &spi3.bus, FLASH_CS_PORT, FLASH_CS_PIN);

	spi_flash_init(&nor_flash, &spi3_flash.dev);

	flash_vol_static_init(&lvs, &nor_flash.flash);
	flash_vol_static_create(&lvs, "boot", 0x0, 0x80000, &lv_boot);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_boot, "boot");
	flash_vol_static_create(&lvs, "system", 0x80000, 0x80000, &lv_system);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_system, "system");
	flash_vol_static_create(&lvs, "test", 0x100000, 0x100000, &lv_test);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_test, "test");

	fs_spiffs_init(&spiffs_system);
	if (fs_spiffs_mount(&spiffs_system, lv_system) == FS_SPIFFS_RET_OK) {
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FS, (Interface *)&spiffs_system.iface, "system");
	} else {
		/* We cannot continue if mounting fails. There is no important data on
		 * the system volume, let's format it to make the thing working again. */
		fs_spiffs_format(&spiffs_system, lv_system);
	}

	if (flash_fifo_init(&fifo, lv_test) == FLASH_FIFO_RET_OK) {
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FS, (Interface *)&fifo.fs, "fifo");
	} else {
		/** @TODO format the FIFO if init fails */
	}
}

/**********************************************************************************************************************
 * PCB temperature sensor
 **********************************************************************************************************************/

static const struct sensor_info pcb_temp_info = {
	.description = "PCB temperature",
	.unit = "°C"
};

Stm32Adc adc1;
AdcSensor pcb_temp;
static void temp_sensor_init(void) {
	stm32_adc_init(&adc1, ADC1);
	adc_sensor_init(&pcb_temp, &adc1.iface, 1, &pcb_temp_info);
	pcb_temp.oversample = 16;
	pcb_temp.div_low_fs = 4095.0f;
	pcb_temp.div_low_high_r = 10000.0f;

	pcb_temp.ntc_r25 = 10000.0f;
	pcb_temp.ntc_beta = 3380.0f;

	// pcb_temp.a = 0.0f;
	// pcb_temp.b = 0.25974f;
	// pcb_temp.c = -259.74f;
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&pcb_temp.iface, "pcb_temp");
}


/**********************************************************************************************************************
 * NBUS/CAN-FD interface
 **********************************************************************************************************************/

Stm32Fdcan can1;
Nbus nbus;
NbusRoot nbus_root;
NbusLog nbus_log;
NbusChannel *nbus_root_channel = &nbus_root.channel;

static void can_init(void) {
	rcc_periph_reset_pulse(RST_FDCAN);
	fdcan_init(CAN1, FDCAN_CCCR_INIT_TIMEOUT);
	fdcan_set_can(CAN1, false, false, true, false, 1, 8, 5, (64e6 / (CAN_BITRATE) / 16) - 1);
	fdcan_set_fdcan(CAN1, true, true, 1, 8, 5, (64e6 / (CAN_BITRATE) / 16) - 1);

	FDCAN_IE(CAN1) |= FDCAN_IE_RF0NE;
	FDCAN_ILE(CAN1) |= FDCAN_ILE_INT0;
	// FDCAN_ILS(CAN1) |= FDCAN_ILS_RxFIFO0;
	// nvic_enable_irq(NVIC_FDCAN1_INTR0_IRQ);
	fdcan_start(CAN1, FDCAN_CCCR_INIT_TIMEOUT);

	stm32_fdcan_init(&can1, CAN1);
	/* TIL: first set the interrupt priority, THEN enable it. */
	nvic_set_priority(NVIC_FDCAN1_INTR1_IRQ, 6 * 16);
	nvic_enable_irq(NVIC_FDCAN1_INTR1_IRQ);

	nbus_init(&nbus, &can1.iface);

	nbus_root_init(&nbus_root, &nbus, UNIQUE_ID_REG, UNIQUE_ID_REG_LEN);
	nbus_log_init(&nbus_log, nbus_root_channel);
}


void fdcan1_intr1_isr(void) {
	/* Toggle status LED directly. */
	if (FDCAN_IR(CAN1) & FDCAN_IR_RF0N) {
		gpio_toggle(GPIOB, GPIO6);
	}
	stm32_fdcan_irq_handler(&can1);
}


/**********************************************************************************************************************
 * I2C EEPROM memory init
 **********************************************************************************************************************/

Stm32I2c i2c1;
I2cEeprom eeprom1;
static void port_i2c_init(void) {
	rcc_periph_clock_enable(RCC_I2C1);
	stm32_i2c_init(&i2c1, I2C1);

	i2c_eeprom_init(&eeprom1, &i2c1.bus, 0x50, 32768, 32);
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
	gpio_mode_setup(EXC_DIS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, EXC_DIS_PIN);
	gpio_set_output_options(EXC_DIS_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, EXC_DIS_PIN);
	gpio_set(EXC_DIS_PORT, EXC_DIS_PIN);

	gpio_mode_setup(ADC_MCLK_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, ADC_MCLK_PIN);
	gpio_set_af(ADC_MCLK_PORT, ADC_MCLK_AF, ADC_MCLK_PIN);

	gpio_mode_setup(ADC_IRQ_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, ADC_IRQ_PIN);

	/* ADC SPI port */
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO13 | GPIO14 | GPIO15);
	gpio_set_af(GPIOB, GPIO_AF5, GPIO13 | GPIO14 | GPIO15);

	/* ADC CS port */
	gpio_set(ADC_CS_PORT, ADC_CS_PIN);
	gpio_mode_setup(ADC_CS_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, ADC_CS_PIN);

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

	/* PCB temperature sensor */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO0);

	/* CAN port. Enable the transceiver permanently (SHDN = L). */
	gpio_mode_setup(CAN_SHDN_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, CAN_SHDN_PIN);
	gpio_clear(CAN_SHDN_PORT, CAN_SHDN_PIN);
	gpio_mode_setup(CAN_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, CAN_RX_PIN);
	gpio_set_af(CAN_RX_PORT, CAN_AF, CAN_RX_PIN);
	gpio_mode_setup(CAN_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, CAN_TX_PIN);
	gpio_set_af(CAN_TX_PORT, CAN_AF, CAN_TX_PIN);

	/* I2C1 port */
	gpio_mode_setup(I2C1_SDA_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, I2C1_SDA_PIN);
	gpio_set_output_options(I2C1_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, I2C1_SDA_PIN);
	gpio_set_af(I2C1_SDA_PORT, I2C1_SDA_AF, I2C1_SDA_PIN);
	gpio_mode_setup(I2C1_SCL_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, I2C1_SCL_PIN);
	gpio_set_output_options(I2C1_SCL_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, I2C1_SCL_PIN);
	gpio_set_af(I2C1_SCL_PORT, I2C1_SCL_AF, I2C1_SCL_PIN);

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
	nor_flash_init();
	temp_sensor_init();
	can_init();
	port_i2c_init();

	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_world, "hello-world");
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&tempco_calibration, "tempco-calibration");

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



