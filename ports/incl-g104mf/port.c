/* SPDX-License-Identifier: BSD-2-Clause
 *
 * incl-g104mf accelerometer/inclinometer port specific code
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/lptimer.h>
#include <libopencm3/stm32/dbgmcu.h>
#include <libopencm3/stm32/quadspi.h>

#include <main.h>
#include "port.h"

/** @TODO redo module and interface, keep the init here. */
#include "module_led.h"
#include "interface_led.h"

/** @TODO redo the whole SPI layer and flash driver */
#include "interface_spibus.h"
#include "module_spibus_locm3.h"
#include "interface_flash.h"
#include "module_spi_flash.h"
#include "interface_spidev.h"
#include "module_spidev_locm3.h"

#include <interfaces/sensor.h>
#include <interfaces/cellular.h>
#include <interfaces/servicelocator.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/stream.h>
#include <interfaces/uart.h>
#include <interfaces/clock.h>

#include <services/cli/system_cli_tree.h>
#include <services/stm32-system-clock/clock.h>
#include <services/stm32-rtc/rtc.h>
#include <services/icm42688p/icm42688p.h>
#include <services/stm32-i2c/stm32-i2c.h>
#include <services/spi-sd/spi-sd.h>
#include <services/bq35100/bq35100.h>
#include <services/gps-ublox/gps-ublox.h>
#include <services/lora-rn2483/lora-rn2483.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/si7006/si7006.h>

#define MODULE_NAME CONFIG_PORT_NAME

/**
 * Port specific global variables and singleton instances.
 */

uint32_t SystemCoreClock;


/** @TODO those defines are missing in the current version od libopencm3 */
#define USART_CR3_UCESM (1 << 23)
#define USART_CR3_WUS_START (2 << 20)


#if defined(CONFIG_SERVICE_STM32_WATCHDOG)
	#include "services/stm32-watchdog/watchdog.h"
	Watchdog watchdog;
#endif

SystemClock system_clock;
Stm32Rtc rtc;
Clock *rtc_clock = &rtc.clock;
Icm42688p accel2;
Bq35100 bq35100;
GpsUblox gps;

#define RCC_CCIPR_LPTIM1SEL_LSE (3 << 18)
volatile uint16_t last_tick;
volatile uint16_t lptim1_ext;

void vPortSetupTimerInterrupt(void);
void port_sleep(TickType_t idle_time);


/*********************************************************************************************************************
 * QSPI NOR flash related configuration and init
 *********************************************************************************************************************/

#if defined(CONFIG_INCL_G104MF_ENABLE_QSPI)

	/* Some art to visualise the structure */
	#include <services/stm32-qspi-flash/stm32-qspi-flash.h>
	Stm32QspiFlash qspi_flash;

		#include <services/flash-vol-static/flash-vol-static.h>
		FlashVolStatic lvs;

			Flash *lv_system;
			#include <services/fs-spiffs/fs-spiffs.h>
			FsSpiffs spiffs_system;

			Flash *lv_fifo;
			#include <services/flash-fifo/flash-fifo.h>
			FlashFifo fifo;

			Flash *lv_test;


	static void port_qspi_init(void) {
		/* Initialize GPIO. Don't forget to set 50 MHz driving strength, otherwise the flash
		 * refuses to work with anything faster than about 1 MHz. */
		gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6 | GPIO7);
		gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO6 | GPIO7);
		gpio_set_af(GPIOA, GPIO_AF10, GPIO6 | GPIO7);
		gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO0 | GPIO1 | GPIO10 | GPIO11);
		gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO0 | GPIO1 | GPIO10 | GPIO11);
		gpio_set_af(GPIOB, GPIO_AF10, GPIO0 | GPIO1 | GPIO10 | GPIO11);

		rcc_periph_clock_enable(RCC_QSPI);
		rcc_periph_reset_pulse(RST_QSPI);
		stm32_qspi_flash_init(&qspi_flash);
		stm32_qspi_flash_set_prescaler(&qspi_flash, 0);

		/* This port is using static flash volumes. Configure some and advertise them. */
		flash_vol_static_init(&lvs, &qspi_flash.iface);
		flash_vol_static_create(&lvs, "system", 0x0, 0x100000, &lv_system);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_system, "system");
		flash_vol_static_create(&lvs, "fifo", 0x100000, 0x400000, &lv_fifo);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_fifo, "fifo");
		flash_vol_static_create(&lvs, "test", 0x500000, 0x100000, &lv_test);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_test, "test");

		fs_spiffs_init(&spiffs_system);
		if (fs_spiffs_mount(&spiffs_system, lv_system) == FS_SPIFFS_RET_OK) {
			iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FS, (Interface *)&spiffs_system.iface, "system");
		} else {
			/* We cannot continue if mounting fails. There is no important data on
			 * the system volume, let's format it to make the thing working again. */
			fs_spiffs_format(&spiffs_system, lv_system);
		}

		if (flash_fifo_init(&fifo, lv_fifo) == FLASH_FIFO_RET_OK) {
			iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FS, (Interface *)&fifo.fs, "fifo");
		} else {
			/** @TODO format the FIFO if init fails */
		}
	}
#endif /* CONFIG_INCL_G104MF_ENABLE_QSPI */


#if defined(CONFIG_INCL_G104MF_QSPI_STARTUP_TEST)
	#include <services/flash-test/flash-test.h>
	FlashTest flash_test;

	static void port_flash_test(void) {
		flash_test_init(&flash_test, &rtc.clock);
		flash_test_dev(&flash_test, lv_test, qspi_flash.info->type);
		flash_test_free(&flash_test);
	}
#endif /* CONFIG_INCL_G104MF_QSPI_STARTUP_TEST */


/*********************************************************************************************************************
 * Initialize peripherals needed to start the scheduler (early init)
 *********************************************************************************************************************/

int32_t port_early_init(void) {
	/* Relocate the vector table first if required. */
	#if defined(CONFIG_RELOCATE_VECTOR_TABLE)
		SCB_VTOR = CONFIG_VECTOR_TABLE_ADDRESS;
	#endif

	#if defined(CONFIG_INCL_G104MF_CLOCK_HSI16)
		rcc_osc_on(RCC_HSI16);
		rcc_wait_for_osc_ready(RCC_HSI16);

		/* Set HSI16 to be wakeup clock source and the system clock source. */
		RCC_CFGR |= RCC_CFGR_STOPWUCK_HSI16;
		rcc_set_sysclk_source(RCC_HSI16);
		SystemCoreClock = 16e6;

		/* We can disable the default MSI clock to save power. */
		rcc_osc_off(RCC_MSI);
	#else
		#error "no clock speed defined"
	#endif

	#if defined(CONFIG_STM32_DEBUG_IN_STOP)
		/* Leave SWD running in STOP1 mode for development. */
		DBGMCU_CR |= DBGMCU_CR_STOP;
	#endif

	rcc_apb1_frequency = SystemCoreClock;
	rcc_apb2_frequency = SystemCoreClock;

	rcc_periph_clock_enable(RCC_SYSCFG);

	/* PWR peripheral for setting sleep/stop modes. */
	rcc_periph_clock_enable(RCC_PWR);
	PWR_CR1 |= PWR_CR1_DBP;

	/* A TCXO is used as a LSE. The oscillator needs to be bypassed. */
	RCC_BDCR |= RCC_BDCR_LSEBYP;
	rcc_osc_on(RCC_LSE);
	rcc_wait_for_osc_ready(RCC_LSE);

	/* Enable low power timer for systick and configure it to use LSE (TCXO). */
	RCC_CCIPR |= RCC_CCIPR_LPTIM1SEL_LSE;
	rcc_periph_clock_enable(RCC_LPTIM1);
	RCC_APB1RSTR1 |= RCC_APB1RSTR1_LPTIM1RST;
	RCC_APB1RSTR1 &= ~RCC_APB1RSTR1_LPTIM1RST;

	/* Initialize all required clocks for GPIO ports. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOH);

	/* Configure clocks in stop modes. */
	RCC_AHB1SMENR = 0;
	RCC_AHB2SMENR = 0;
	RCC_AHB3SMENR = 0;
	RCC_APB1SMENR1 = RCC_APB1SMENR1_LPTIM1SMEN | RCC_APB1SMENR1_USART2SMEN;
	RCC_APB1SMENR2 = 0;
	RCC_APB2SMENR = RCC_APB2SMENR_USART1SMEN;

	return PORT_EARLY_INIT_OK;
}


/*********************************************************************************************************************
 * Debugger related init
 * @TODO create a separate service to handle debugger connect/disconnect (even in runtime)
 *********************************************************************************************************************/

static void port_check_debug(void) {
	/* Check if the debugger cable is physically attached */
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO13 | GPIO14);
	vTaskDelay(10);
	if (gpio_get(GPIOA, GPIO13)) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("debugger not connected, disabling debug features"));
		/* Configure SWD pins as output to enable LEDs */
		gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13 | GPIO14);
		DBGMCU_CR &= ~DBGMCU_CR_STOP;
		DBGMCU_CR &= ~DBGMCU_CR_SLEEP;
	} else {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("debugger connected"));
		/* Configure back as SWD */
		gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO13 | GPIO14);
	}

	if (SCS_DHCSR & SCS_DHCSR_C_DEBUGEN) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("debugger enabled"));
	}

	/* Beware of keeping the debugger enabled in STOP mode, this increases power consumption considerably. */
	if (DBGMCU_CR & DBGMCU_CR_STOP) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("debugging in STOP mode"));
	}
}


void port_enable_swd(bool e) {
	if (e) {
		gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO13 | GPIO14);
		#if defined(CONFIG_STM32_DEBUG_IN_STOP)
			DBGMCU_CR |= DBGMCU_CR_STOP;
		#endif
	} else {
		gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13 | GPIO14);
		DBGMCU_CR &= ~DBGMCU_CR_STOP;
	}
}


static int64_t timespec_diff(struct timespec *time1, struct timespec *time2) {
	return (time1->tv_sec * 1000000000UL + time1->tv_nsec)
	     - (time2->tv_sec * 1000000000UL + time2->tv_nsec);
}


/*********************************************************************************************************************
 * System serial console initialization
 *********************************************************************************************************************/

#if defined(CONFIG_INCL_G104MF_ENABLE_CONSOLE)
	Stm32Uart console;
	static void console_init(void) {
		gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO9 | GPIO10);
		gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO9 | GPIO10);
		gpio_set_af(GPIOA, GPIO_AF7, GPIO9 | GPIO10);

		/* Main system console is created on the first USART interface. Enable
		 * USART clocks and interrupts and start the corresponding HAL module. */
		rcc_periph_clock_enable(RCC_USART1);

		/* Allow waking up in stop mode. */
		USART_CR1(USART1) |= USART_CR1_UESM;
		USART_CR3(USART1) |= USART_CR3_WUS_RXNE;
		RCC_CCIPR |= (RCC_CCIPR_USARTxSEL_HSI16 << RCC_CCIPR_USART1SEL_SHIFT);

		nvic_enable_irq(NVIC_USART1_IRQ);
		nvic_set_priority(NVIC_USART1_IRQ, 6 * 16);
		stm32_uart_init(&console, USART1);
		console.uart.vmt->set_bitrate(&console.uart, CONFIG_INCL_G104MF_CONSOLE_SPEED);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&console.stream, "console");

		/* Console is now initialized, set its stream to be used as u_log output. */
		u_log_set_stream(&(console.stream));
	}


	void usart1_isr(void) {
		stm32_uart_interrupt_handler(&console);
	}
#endif


/*********************************************************************************************************************
 * LoRaWAN modem
 *********************************************************************************************************************/

#if defined(CONFIG_INCL_G104MF_ENABLE_LORA)
	Stm32Uart lora_uart;
	LoraModem rn2483_lora;

	static power_ret_t port_lora_power_enable(Power *self, bool enable) {
		(void)self;
		if (enable) {
			gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO2 | GPIO3);
			gpio_set(GPIOA, GPIO0);
		} else {
			gpio_clear(GPIOA, GPIO0);
			gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO2 | GPIO3);
		}
		return POWER_RET_OK;
	}

	const struct power_vmt port_lora_power_vmt = {
		.enable = port_lora_power_enable
	};

	Power rn2483_power;

	static void rn2483_lora_init(void) {
		/* LORA_PWR_EN */
		gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0);
		rn2483_power.vmt = &port_lora_power_vmt;

		gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO2 | GPIO3);
		gpio_set_af(GPIOA, GPIO_AF7, GPIO2 | GPIO3);

		rcc_periph_clock_enable(RCC_USART2);

		/* Allow waking up in stop mode. */
		USART_CR1(USART2) |= USART_CR1_UESM;
		USART_CR3(USART2) |= USART_CR3_WUS_RXNE;
		RCC_CCIPR |= RCC_CCIPR_USARTxSEL_HSI16 << RCC_CCIPR_USART2SEL_SHIFT;

		nvic_enable_irq(NVIC_USART2_IRQ);
		nvic_set_priority(NVIC_USART2_IRQ, 6 * 16);
		stm32_uart_init(&lora_uart, USART2);
		lora_uart.uart.vmt->set_bitrate(&lora_uart.uart, 57600);

		lora_modem_init(&rn2483_lora, &lora_uart.stream, &rn2483_power);
		lora_modem_set_ar(&rn2483_lora, true);

		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_LORA, (Interface *)&rn2483_lora.lora, "rn2483");
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&rn2483_lora.vdd, "rn2483-vdd");
	}


	void usart2_isr(void) {
		stm32_uart_interrupt_handler(&lora_uart);
	}
#endif


/*********************************************************************************************************************
 * Dual colour LED
 *********************************************************************************************************************/

#if defined(CONFIG_INCL_G104MF_ENABLE_LEDS)
	struct module_led led_status;
	struct module_led led_error;

	static void led_init(void) {
		module_led_init(&led_status, "led_status");
		module_led_set_port(&led_status, GPIOA, GPIO14);
		// interface_led_loop(&led_status.iface, 0x111f);
		interface_led_loop(&led_status.iface, 0xf);
		hal_interface_set_name(&(led_status.iface.descriptor), "led_status");
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_LED, (Interface *)&led_status.iface.descriptor, "led_status");

		module_led_init(&led_error, "led_error");
		module_led_set_port(&led_error, GPIOA, GPIO13);
		interface_led_loop(&led_error.iface, 0xf);
		hal_interface_set_name(&(led_error.iface.descriptor), "led_error");
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_LED, (Interface *)&led_error.iface.descriptor, "led_error");
	}
#endif




/*********************************************************************************************************************
 * TDK ICM42688P accelerometer
 *********************************************************************************************************************/

#if defined(CONFIG_INCL_G104MF_ENABLE_TDK_ACCEL) || defined(CONFIG_INCL_G104MF_ENABLE_RFM)
	Stm32SpiBus spi1;

	static void port_spi1_init(void) {
		/* SPI1 with three accelerometers and a radio */
		gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO3 | GPIO4 | GPIO5);
		gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO3 | GPIO4 | GPIO5);
		gpio_set_af(GPIOB, GPIO_AF5, GPIO3 | GPIO4 | GPIO5);
		rcc_periph_clock_enable(RCC_SPI1);

		stm32_spibus_init(&spi1, SPI1);
		spi1.bus.vmt->set_sck_freq(&spi1.bus, 10e6);
		spi1.bus.vmt->set_mode(&spi1.bus, 0, 0);

	}
#endif


#if defined(CONFIG_INCL_G104MF_ENABLE_TDK_ACCEL)
	Stm32SpiDev spi1_accel2;

	void port_accel2_init(void) {
		/* ICM42688P on SPI1 bus */
		gpio_set(GPIOB, GPIO8);
		gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO8);
		gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO8);

		stm32_spidev_init(&spi1_accel2, &spi1.bus, GPIOB, GPIO8);

		/* SYNC32 output */
		gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO2);

		icm42688p_init(&accel2, &spi1_accel2.dev);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_WAVEFORM_SOURCE, (Interface *)&accel2.source, "accel2");
	}
#endif


/*********************************************************************************************************************
 * RFMxx LoRa radio transceiver
 *********************************************************************************************************************/

#if defined(CONFIG_INCL_G104MF_ENABLE_RFM)
	struct module_spidev_locm3 spi1_rfm;

	void port_rfm_init(void) {
		/* SX127x on SPI1 bus */
		gpio_set(GPIOC, GPIO15);
		gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO15);
		gpio_set_output_options(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO15);
		module_spidev_locm3_init(&spi1_rfm, "spi1_rfm", &(spi1.iface), GPIOC, GPIO15);
		hal_interface_set_name(&(spi1_rfm.iface.descriptor), "spi1_rfm");

		/* Put the SX127x to idle sleep. */
		vTaskDelay(2);
		uint8_t rfm_seq_idle[] = {0x80 | 0x36, 0x20};
		interface_spidev_select(&spi1_rfm.iface);
		interface_spidev_send(&spi1_rfm.iface, rfm_seq_idle, sizeof(rfm_seq_idle));
		interface_spidev_deselect(&spi1_rfm.iface);

		vTaskDelay(2);
		uint8_t rfm_standby[] = {0x81, 0x00};
		interface_spidev_select(&spi1_rfm.iface);
		interface_spidev_send(&spi1_rfm.iface, rfm_standby, sizeof(rfm_standby));
		interface_spidev_deselect(&spi1_rfm.iface);
	}
#endif


/*********************************************************************************************************************
 * micro-SD card initialization and support functions
 *********************************************************************************************************************/

#if defined(CONFIG_INCL_G104MF_ENABLE_MICROSD)
	struct module_spibus_locm3 spi2;
	struct module_spidev_locm3 spi2_sd;

	void port_sd_power(bool power) {
		if (power) {
			gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
			gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO13 | GPIO14 | GPIO15);
			/* SD_SEL */
			gpio_clear(GPIOA, GPIO5);
			/* SD_PWR_EN */
			gpio_set(GPIOH, GPIO3);
		} else {
			gpio_clear(GPIOH, GPIO3);
			gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO13 | GPIO14 | GPIO15);
		}
	}


	void port_sd_init(void) {
		/* SD_SEL */
		gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO5);
		gpio_clear(GPIOA, GPIO5);

		/* SD_PWR_EN, default powered off */
		gpio_mode_setup(GPIOH, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO3);
		gpio_clear(GPIOH, GPIO3);

		/* SPI2 */
		gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO13 | GPIO14 | GPIO15);
		gpio_set_af(GPIOB, GPIO_AF5, GPIO13 | GPIO14 | GPIO15);
		rcc_periph_clock_enable(RCC_SPI2);
		module_spibus_locm3_init(&spi2, "spi2", SPI2);
		hal_interface_set_name(&(spi2.iface.descriptor), "spi2");

		gpio_set(GPIOB, GPIO12);
		gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
		gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO12);
		module_spidev_locm3_init(&spi2_sd, "spi2_sd", &(spi2.iface), GPIOB, GPIO12);
		hal_interface_set_name(&(spi2_sd.iface.descriptor), "spi2_sd");

		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SPIDEV, (Interface *)&spi2_sd.iface.descriptor, "spi-sd");
	}
#endif


/*********************************************************************************************************************
 * I2C bus and battery gauge
 *********************************************************************************************************************/

Stm32I2c i2c1;
static void port_i2c_init(void) {
	rcc_periph_clock_enable(RCC_I2C1);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6 | GPIO9);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, GPIO6 | GPIO9);
	gpio_set_af(GPIOB, GPIO_AF4, GPIO6 | GPIO9);
	stm32_i2c_init(&i2c1, I2C1);
}


void port_battery_gauge_init(void) {
	/* BQ35100 test */
	if (bq35100_init(&bq35100, &i2c1.bus) == BQ35100_RET_OK) {
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&bq35100.voltage, "vbat");
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&bq35100.current, "ibat");
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&bq35100.bat_temp, "bat_temp");
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&bq35100.die_temp, "bq35100_temp");
	}
	// while (false) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("bat U = %u mV, I = %d mA"), bq35100_voltage(&bq35100), bq35100_current(&bq35100));
		// vTaskDelay(1000);
	// }
}


Si7006 si7006;
Sensor *si7006_sensor = &si7006.temperature;
static void port_temp_sensor_init(void) {
	if (si7006_init(&si7006, &i2c1.bus) == SI7006_RET_OK) {
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&si7006.temperature, "temperature");
	}
}

/*********************************************************************************************************************
 * u-blox MAX-M8Q initialization & ISR
 *********************************************************************************************************************/

void exti9_5_isr(void) {
	exti_reset_request(EXTI8);
	gps_ublox_timepulse_handler(&gps);
}


void port_gps_init(void) {
	gps_ublox_init(&gps);
	gps_ublox_set_i2c_transport(&gps, &i2c1.bus, 0x42);
	gps.measure_clock = &rtc.clock;
	//gps_ublox_mode(&gps, GPS_UBLOX_MODE_BACKUP);

	/* Enable 1PPS interrupt when the driver is properly initialized. */
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO8);
	nvic_enable_irq(NVIC_EXTI9_5_IRQ);
	nvic_set_priority(NVIC_EXTI9_5_IRQ, 5 * 16);
	exti_select_source(EXTI8, GPIOA);
	exti_set_trigger(EXTI8, EXTI_TRIGGER_RISING);

	#if 0
		#include "gps-test-code.c"
	#endif
}


void port_gps_power(bool en) {
	if (en) {
		gpio_set(GPIOH, GPIO1);
		vTaskDelay(100);
	} else {
		gpio_clear(GPIOH, GPIO1);
	}
}

/*********************************************************************************************************************
 * Default GPIO configuration. Put all peripherals in the lowest current consumption setting.
 *********************************************************************************************************************/

static void port_setup_default_gpio(void) {
	/* ADXL power off, GPS power off. */
	gpio_mode_setup(GPIOH, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0 | GPIO1);
	gpio_clear(GPIOH, GPIO0 | GPIO1);

	/* ACCEL1_CS */
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13);
	gpio_set(GPIOC, GPIO13);

	/* RFM_CS */
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO15);
	gpio_set(GPIOC, GPIO15);

	/* USB_VBUS_DET */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO4);

	/* MCU_USB_VBUS_DET */
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO4);

	/* QSPI_BK1_NCS */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO11);
	gpio_set(GPIOB, GPIO11);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO3 | GPIO4 | GPIO5);
	gpio_set_af(GPIOB, GPIO_AF5, GPIO3 | GPIO4 | GPIO5);

	/* USB pins */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO11 | GPIO12);

	/* I2C */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO6 | GPIO9);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, GPIO6 | GPIO9);
	gpio_set(GPIOB, GPIO6 | GPIO9);

	/* ACCEL_INT */
	gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO7);
}


/*********************************************************************************************************************
 * Main initialisaton routine (run in the init task)
 *********************************************************************************************************************/


int32_t port_init(void) {
	port_setup_default_gpio();
	console_init();
	#if defined(CONFIG_INCL_G104MF_ENABLE_WATCHDOG)
		watchdog_init(&watchdog, 20000, 0);
	#endif
	stm32_rtc_init(&rtc);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CLOCK, &rtc.iface.interface, "rtc");
	#if defined(CONFIG_INCL_G104MF_ENABLE_LEDS)
		led_init();
	#endif

	port_i2c_init();
	port_temp_sensor_init();
	port_battery_gauge_init();
	port_gps_power(false);
	// port_gps_init();
	port_sd_init();
	rn2483_lora_init();

	#if defined(CONFIG_INCL_G104MF_ENABLE_TDK_ACCEL) || defined(CONFIG_INCL_G104MF_ENABLE_RFM)
		port_spi1_init();
	#endif

	#if defined(CONFIG_INCL_G104MF_ENABLE_TDK_ACCEL)
		port_accel2_init();
	#endif

	#if defined(CONFIG_INCL_G104MF_ENABLE_RFM)
		port_rfm_init();
	#endif

	#if defined(CONFIG_SERVICE_STM32_QSPI_FLASH)
		port_qspi_init();

		#if defined(CONFIG_INCL_G104MF_QSPI_STARTUP_TEST)
		port_flash_test();
		#endif
	#endif

	port_check_debug();

	return PORT_INIT_OK;
}


void vApplicationIdleHook(void);
void vApplicationIdleHook(void) {
}

/*********************************************************************************************************************
 * Low power timer (LPTIM) for FreeRTOS timing
 *********************************************************************************************************************/

#define PORT_LPTIM_ARR (0x8000)
void vPortSetupTimerInterrupt(void) {
	lptimer_disable(LPTIM1);
	lptimer_set_internal_clock_source(LPTIM1);
	lptimer_enable_trigger(LPTIM1, LPTIM_CFGR_TRIGEN_SW);
	lptimer_set_prescaler(LPTIM1, LPTIM_CFGR_PRESC_1);

	lptimer_enable(LPTIM1);
	lptimer_set_period(LPTIM1, PORT_LPTIM_ARR - 1);
	lptimer_set_compare(LPTIM1, 0 + 31);
	lptimer_enable_irq(LPTIM1, LPTIM_IER_CMPMIE);
	nvic_set_priority(NVIC_LPTIM1_IRQ, 5 * 16);
	nvic_enable_irq(NVIC_LPTIM1_IRQ);
	lptimer_start_counter(LPTIM1, LPTIM_CR_CNTSTRT);
}

void port_task_timer_init(void) {
	/* LPTIM1 us used for task statistics. It is already initialized. */
}


uint32_t port_task_timer_get_value(void) {
	return lptim_get_extended();
}


static void lptim_schedule_cmp(uint32_t lptim, uint16_t increment_ticks, uint16_t *current_ticks) {
	uint16_t this_tick = 0;
	while (this_tick != lptimer_get_counter(lptim)) {
		this_tick = lptimer_get_counter(lptim);
	}
	uint16_t next_tick = (this_tick + increment_ticks) % PORT_LPTIM_ARR;

	/* Cannot write 0xffff to the compare register. Cheat a bit in this case. */
	if (next_tick == 0xffff) {
		next_tick = 0;
	}
	lptimer_set_compare(lptim, next_tick);
	if (current_ticks != NULL) {
		*current_ticks = this_tick;
	}
}


static uint16_t lptim_time_till_now(uint32_t lptim, uint16_t last_time) {
	uint16_t time_now = 0;
	while (time_now != lptimer_get_counter(lptim)) {
		time_now = lptimer_get_counter(lptim);
	}

	return time_now - last_time;
}


/*********************************************************************************************************************
 * FreeRTOS tickless implementation using LPTIM1
 *********************************************************************************************************************/

volatile bool systick_enabled = true;
void lptim1_isr(void) {
	if (lptimer_get_flag(LPTIM1, LPTIM_ISR_CMPM)) {
		lptimer_clear_flag(LPTIM1, LPTIM_ICR_CMPMCF);
			if (systick_enabled) {
				xPortSysTickHandler();
				lptim_schedule_cmp(LPTIM1, 32, NULL);
			}

	}
	if (lptimer_get_flag(LPTIM1, LPTIM_ISR_ARRM)) {
		lptimer_clear_flag(LPTIM1, LPTIM_ICR_ARRMCF);
			gpio_set(GPIOB, GPIO2);
			lptim1_ext++;
			gpio_clear(GPIOB, GPIO2);
	}
}


uint32_t lptim_get_extended(void) {
	uint16_t e = lptim1_ext;
	uint16_t b = lptimer_get_counter(LPTIM1);
	uint16_t e2 = lptim1_ext;
	if (e != e2) {
		if (b > 32768) {
			return (e << 16) | b;
		} else {
			return (e2 << 16) | b;
		}
	}
	return (e << 16) | b;
}


static void port_wait_sleep(void) {
	while (
		((USART_CR1(USART1) & USART_CR1_UE) && ((USART_ISR(USART1) & USART_ISR_TC) == 0)) ||
		((USART_CR1(USART2) & USART_CR1_UE) && ((USART_ISR(USART2) & USART_ISR_TC) == 0))
	) {
		;
	}
}

void port_sleep(TickType_t idle_time) {
	eSleepModeStatus sleep_status;

	/* Wait until all IO transmissions are completed.
	 * Going to sleep in the middle of a tx/rx would corrupt the data. */
	port_wait_sleep();

	if (idle_time > 1000) {
		idle_time = 1000;
	}

	__asm volatile("cpsid i");
	__asm volatile("dsb");
	__asm volatile("isb");

	sleep_status = eTaskConfirmSleepModeStatus();
	if (sleep_status == eAbortSleep) {
		__asm volatile("cpsie i");

	} else {
		systick_enabled = false;

		/* Enter STOP 1 mode. Set LPMS to 001, SLEEPDEEP bit and do a WFI later. */
		SCB_SCR |= SCB_SCR_SLEEPDEEP;
		PWR_CR1 &= ~PWR_CR1_LPMS_MASK;
		PWR_CR1 |= PWR_CR1_LPMS_STOP_1;

		/* Wake up in the future. Next tick is calculated and properly cropped inside */
		uint16_t time_before_sleep = 0;
		lptim_schedule_cmp(LPTIM1, idle_time * 32, &time_before_sleep);

		__asm volatile("dsb");
		__asm volatile("wfi");
		__asm volatile("isb");

		uint16_t ticks_completed = lptim_time_till_now(LPTIM1, time_before_sleep) / 32;
		if (ticks_completed > idle_time) {
			ticks_completed = idle_time;
		}
		vTaskStepTick(ticks_completed);
	}

	lptim_schedule_cmp(LPTIM1, 32, NULL);
	__asm volatile("cpsie i");
	systick_enabled = true;
}
