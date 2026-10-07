/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 basic port (STM32U575VIT6)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <stm32u5xx.h>

#include <main.h>
#include "port.h"

#include <interfaces/servicelocator.h>

/* Low level drivers for the STM32U5 family */
#include <services/stm32-gpio/stm32-gpio.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-octospi-flash/stm32-octospi-flash.h>
#include <services/stm32-flash/stm32-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>
#include <services/stm32-i2c/stm32-i2c.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/lcd-st7586/lcd-st7586.h>
#include <services/lp581x-led/lp581x-led.h>
#include <services/lp586x-led/lp586x-led.h>
#include <services/gpio-led/gpio-led.h>
#include <services/stm32-timer/stm32-timer.h>
#include <services/pwm-beeper/pwm-beeper.h>
#include <services/pcal6408a-gpio/pcal6408a-gpio.h>
#include <services/gpio-keypad/gpio-keypad.h>
#include <services/keypad-layout/keypad-layout.h>
#include <interfaces/led-sequences.h>
#include <interfaces/beeper-sequences.h>
#include <interfaces/event.h>

/* The power manager owns the system clock scaling and runs in both the application and the bootloader.
 * system-conf is advertised unconditionally (see port_setup_system_conf), so its header is needed in
 * both builds too. */
#include <services/system-conf/system-conf.h>
#include <services/pm-generic/pm-generic.h>
#include <interfaces/pm.h>

#if !defined(CONFIG_APP_BL)
#include <services/ncn26010/ncn26010.h>
#include <services/st67w611/st67w611.h>
#include <services/bq25798/bq25798.h>
#include <services/bq27441/bq27441.h>
#include <services/flash-cbor-mib/flash-cbor-mib.h>
#include <interfaces/applet.h>
#include <applets/hello-world/hello-world.h>
#include <applets/hello-wren/generated/hello-wren.h>
#include <applets/settings/settings.h>
#include <applets/live-data/live-data.h>
#endif


#define MODULE_NAME "port"


/**
 * Port specific global variables and singleton instances.
 */

uint32_t SystemCoreClock;

Stm32Gpio gpioa;
Stm32Gpio gpiob;
Stm32Gpio gpioc;
Stm32Gpio gpiod;
Stm32Gpio gpioe;
Stm32OctospiFlash octospi_flash;

/* Internal flash of the STM32U575. The bootloader lives at the start and the application image is placed
 * right after it; both the chainloader and the flash updater target this internal flash. */
Stm32Flash iflash;
FlashVolStatic iflash_vol;

Flash *lv_bootloader;
Flash *lv_bootconf;
Flash *lv_mib;
Flash *lv_app;
#if !defined(CONFIG_APP_BL)
FlashCborMib mib;
#endif

/* Partition table carved out of the flash1 QSPI NOR flash. */
FlashVolStatic flash1_vol;

Flash *lv_fw_good;
Flash *lv_fw_update;
Flash *lv_fw_backup;
Flash *lv_applets;
Flash *lv_bl_backup;
Flash *lv_conf;
Flash *lv_calib;
Flash *lv_conf_backup;
Flash *lv_log;


/* Re-arm SysTick for the current SystemCoreClock. The FreeRTOS tick runs off the core clock (HCLK) -- SysTick
 * has no independent clock source on this part, only HCLK or HCLK/8 -- which the D2 <-> D3 transitions scale,
 * so its reload must be recomputed whenever the clock changes to keep the 1 kHz tick period constant. Tickless
 * idle is disabled, so only the reload and current-value registers need updating; the CTRL register (clock
 * source, interrupt and enable, set up by the FreeRTOS port) is left untouched. */
static void port_setup_systick(void) {
	SysTick->LOAD = (SystemCoreClock / CONFIG_FREERTOS_TICK_RATE_HZ) - 1;
	SysTick->VAL = 0;
}


/* Bring the system up to the STM32U575 maximum of 160 MHz off PLL1, fed by the 16 MHz HSE crystal.
 * Reaching 160 MHz needs voltage scaling range 1 with the EPOD booster enabled and 4 flash wait states.
 * PLL1 divides the 16 MHz HSE by M=1, multiplies by N=10 for a 160 MHz VCO and divides by R=1, so the
 * SYSCLK is exactly 160 MHz. The AHB/APB prescalers stay at their /1 reset value: 160 MHz is within the
 * bus limits, so no peripheral clock adjustment is required. This is the high-power clock configuration
 * entered on the D3 -> D2 power transition; it is the exact inverse of port_shutdown_sysclk(). */
static void port_setup_sysclk(void) {
	/* Select voltage scaling range 1 (VOS = 0b11), the only range that allows 160 MHz. */
	PWR->VOSR = (PWR->VOSR & ~PWR_VOSR_VOS) | PWR_VOSR_VOS_0 | PWR_VOSR_VOS_1;
	while ((PWR->VOSR & PWR_VOSR_VOSRDY) == 0) {
		;
	}

	/* Start the 16 MHz HSE crystal that feeds PLL1. */
	RCC->CR |= RCC_CR_HSEON;
	while ((RCC->CR & RCC_CR_HSERDY) == 0) {
		;
	}

	/* PLL1: HSE source, input range 8..16 MHz, EPOD booster prescaler /1 (M=1 keeps the 16 MHz reference),
	 * and enable the R output that feeds SYSCLK. */
	RCC->PLL1CFGR = RCC_PLL1CFGR_PLL1SRC_0 | RCC_PLL1CFGR_PLL1SRC_1 |
	                RCC_PLL1CFGR_PLL1RGE_0 |
	                RCC_PLL1CFGR_PLL1REN;

	/* The EPOD booster is clocked from the PLL1 input divider configured above; enable it and wait until the
	 * boosted core supply is ready. It is mandatory above 55 MHz in range 1. */
	PWR->VOSR |= PWR_VOSR_BOOSTEN;
	while ((PWR->VOSR & PWR_VOSR_BOOSTRDY) == 0) {
		;
	}

	/* Raise the flash latency to 4 wait states (128 < HCLK <= 160 MHz in range 1) before the clock speeds up. */
	FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_4WS;
	while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_4WS) {
		;
	}

	/* N and R are encoded as value-1: N=10 -> field 9, R=1 -> field 0. */
	RCC->PLL1DIVR = ((10 - 1) << RCC_PLL1DIVR_PLL1N_Pos) | ((1 - 1) << RCC_PLL1DIVR_PLL1R_Pos);

	RCC->CR |= RCC_CR_PLL1ON;
	while ((RCC->CR & RCC_CR_PLL1RDY) == 0) {
		;
	}

	/* Switch SYSCLK to PLL1R (SW = 0b11). */
	RCC->CFGR1 = (RCC->CFGR1 & ~RCC_CFGR1_SW) | RCC_CFGR1_SW_0 | RCC_CFGR1_SW_1;
	while ((RCC->CFGR1 & RCC_CFGR1_SWS) != (RCC_CFGR1_SWS_0 | RCC_CFGR1_SWS_1)) {
		;
	}

	SystemCoreClock = 160e6;

	/* Re-arm the FreeRTOS tick for the boosted core clock. */
	port_setup_systick();
}


/* Drop the system back to the low-power slow clock entered on the D2 -> D3 power transition: SYSCLK runs
 * from the 4 MHz MSIS internal oscillator (the reset default) and PLL1 and the HSE crystal are stopped.
 * HSI16 is left running by port_early_init so the I2C buses stay clocked and accessible in D3. The flash
 * wait states, EPOD booster and core voltage are relaxed to match the 4 MHz clock. The inverse of
 * port_setup_sysclk(). */
static void port_shutdown_sysclk(void) {
	/* Switch SYSCLK back to the 4 MHz MSIS (SW = 0b00) before stopping the PLL. */
	RCC->CFGR1 &= ~RCC_CFGR1_SW;
	while ((RCC->CFGR1 & RCC_CFGR1_SWS) != 0) {
		;
	}

	SystemCoreClock = 4e6;

	/* The PLL and the HSE crystal only fed the fast clock; stop both. */
	RCC->CR &= ~RCC_CR_PLL1ON;
	while ((RCC->CR & RCC_CR_PLL1RDY) != 0) {
		;
	}
	RCC->CR &= ~RCC_CR_HSEON;

	/* Relax the flash latency back to 0 wait states and drop the EPOD booster and voltage scaling; none of
	 * them are needed at 4 MHz. */
	FLASH->ACR &= ~FLASH_ACR_LATENCY;
	PWR->VOSR &= ~PWR_VOSR_BOOSTEN;
	PWR->VOSR &= ~PWR_VOSR_VOS;

	/* Re-arm the FreeRTOS tick for the slow core clock. */
	port_setup_systick();
}


int32_t port_early_init(void) {
	/* The PWR registers live behind an RCC clock gate; enable it before touching the voltage scaling. */
	RCC->AHB3ENR |= RCC_AHB3ENR_PWREN;

	/* Keep HSI16 running as a fixed 16 MHz kernel clock for the I2C buses, decoupling their timing from the
	 * SYSCLK: the I2C timing prescaler is only 4 bits wide and cannot divide a fast SYSCLK down far enough.
	 * HSI16 stays up in every power state (including D3) so the I2C buses remain accessible while the fast
	 * clocks are gated. */
	RCC->CR |= RCC_CR_HSION;
	while ((RCC->CR & RCC_CR_HSIRDY) == 0) {
		;
	}
	RCC->CCIPR1 = (RCC->CCIPR1 & ~(RCC_CCIPR1_I2C1SEL | RCC_CCIPR1_I2C2SEL)) |
	              RCC_CCIPR1_I2C1SEL_1 | RCC_CCIPR1_I2C2SEL_1;

	/* The system comes out of reset on the 4 MHz MSIS and stays there: the power manager raises the clock to
	 * the full 160 MHz PLL on the D3 -> D2 transition and drops it back on D2 -> D3. D3 is the default state,
	 * so the slow clock is the boot clock too. */
	SystemCoreClock = 4e6;

	/* Enable full access to the FPU (CP10/CP11) before any floating point code runs. */
	SCB->CPACR |= (0xf << 20);

	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOAEN;
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOBEN;
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOCEN;
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIODEN;
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_GPIOEEN;

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

Stm32Uart uart4;
static void port_setup_console(void) {
	RCC->APB1ENR1 |= RCC_APB1ENR1_UART4EN;

	/* UART4 TX on PA0, RX on PA1, AF8. */
	gpioa.pin[0].vmt->set_mode(&(gpioa.pin[0]), MODE_ALTERNATE);
	gpioa.pin[0].vmt->set_pinmux(&(gpioa.pin[0]), 8);
	gpioa.pin[1].vmt->set_mode(&(gpioa.pin[1]), MODE_ALTERNATE);
	gpioa.pin[1].vmt->set_pinmux(&(gpioa.pin[1]), 8);

	/* Initialise and configure the UART */
	stm32_uart_init(&uart4, (void *)UART4);
	uart4.uart.vmt->set_bitrate(&uart4.uart, 115200);
	stm32_uart_set_rxtx_swap(&uart4, true);

	NVIC_EnableIRQ(UART4_IRQn);
	NVIC_SetPriority(UART4_IRQn, 7);

	/* Advertise the console stream output and set it as default for log output. */
	Stream *console = &uart4.stream;
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)console, "console");
	u_log_set_stream(console);
}


void uart4_isr(void);
void uart4_isr(void) {
	stm32_uart_interrupt_handler(&uart4);
}


/**********************************************************************************************************************
 * nbus2 backplane stream on USART1
 **********************************************************************************************************************/

#if !defined(CONFIG_APP_BL)

/* USART1 carries the nbus2 protocol on the backplane connecting to the measurement card. The port only
 * sets up the USART and advertises its byte stream; the application discovers the stream and builds the
 * nbus2 stack (framing, MAC and the protocols) on top of it. */
Stm32Uart uart1;

Gpio *nbus2_shdn_gpio = &(gpiod.pin[14]);

static void port_setup_nbus2(void) {
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

	/* The nbus2 transceiver shutdown on PD14 is active high; drive it low to enable the transceiver. The
	 * power manager shuts it down again in D3 (and a wake reboots, so this runs fresh on every boot). */
	nbus2_shdn_gpio->vmt->set_mode(nbus2_shdn_gpio, MODE_OUTPUT);
	nbus2_shdn_gpio->vmt->set(nbus2_shdn_gpio, false);

	/* USART1 TX on PA9, RX on PA10, AF7. */
	gpioa.pin[9].vmt->set_mode(&(gpioa.pin[9]), MODE_ALTERNATE);
	gpioa.pin[9].vmt->set_pinmux(&(gpioa.pin[9]), 7);
	gpioa.pin[10].vmt->set_mode(&(gpioa.pin[10]), MODE_ALTERNATE);
	gpioa.pin[10].vmt->set_pinmux(&(gpioa.pin[10]), 7);

	stm32_uart_init(&uart1, (void *)USART1);
	stm32_uart_set_rto(&uart1, true);
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

#endif


/**********************************************************************************************************************
 * OCTOSPI NOR flash initialisation
 **********************************************************************************************************************/

static void port_setup_flash(void) {
	/* Enable the OCTOSPI1 peripheral and its I/O manager. */
	RCC->AHB2ENR1 |= RCC_AHB2ENR1_OCTOSPIMEN;
	RCC->AHB2ENR2 |= RCC_AHB2ENR2_OCTOSPI1EN;

	/* OCTOSPI1 is wired to the I/O manager port 1 on PE10..PE15, AF10:
	 * CLK on PE10, NCS on PE11 and IO0..IO3 on PE12..PE15. Use the highest
	 * slew rate to keep the QSPI signal integrity. */
	for (int i = 10; i <= 15; i++) {
		gpioe.pin[i].vmt->set_mode(&(gpioe.pin[i]), MODE_ALTERNATE);
		gpioe.pin[i].vmt->set_pinmux(&(gpioe.pin[i]), 10);
		gpioe.pin[i].vmt->set_ospeed(&(gpioe.pin[i]), OSPEED_VERYHIGH);
	}

	/* Route OCTOSPI1 (source 0) to the I/O manager physical port 1. */
	OCTOSPIM->PCR[0] = OCTOSPIM_PCR_CLKEN | OCTOSPIM_PCR_NCSEN | OCTOSPIM_PCR_IOLEN;

	if (stm32_octospi_flash_qspi_init(&octospi_flash, (void *)OCTOSPI1) != STM32_OCTOSPI_FLASH_RET_OK) {
		return;
	}
	stm32_octospi_flash_set_prescaler(&octospi_flash, 1);

	/* Advertise the raw flash interface. */
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)&octospi_flash.iface, "flash1");

	/* Carve the flash1 QSPI NOR flash into fixed partitions. Layout (offset -> size):
	 *   0x000000  fw-good      512K
	 *   0x080000  fw-update    512K
	 *   0x100000  fw-backup    512K
	 *   0x180000  applets      512K
	 *   0x200000  bl-backup     64K
	 *   0x210000  conf          64K
	 *   0x220000  calib         64K
	 *   0x230000  conf-backup   64K
	 *   0x240000  log          256K
	 */
	flash_vol_static_init(&flash1_vol, &octospi_flash.iface);

	flash_vol_static_create(&flash1_vol, "fw-good",     0x000000, 512 * 1024, &lv_fw_good);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_fw_good, "fw-good");

	flash_vol_static_create(&flash1_vol, "fw-update",   0x080000, 512 * 1024, &lv_fw_update);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_fw_update, "fw-update");
	/* The bootloader's flash updater looks the staged image up under the generic "update" name. */
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_fw_update, "update");

	flash_vol_static_create(&flash1_vol, "fw-backup",   0x100000, 512 * 1024, &lv_fw_backup);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_fw_backup, "fw-backup");

	flash_vol_static_create(&flash1_vol, "applets",     0x180000, 512 * 1024, &lv_applets);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_applets, "applets");

	flash_vol_static_create(&flash1_vol, "bl-backup",   0x200000, 64 * 1024, &lv_bl_backup);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_bl_backup, "bl-backup");

	flash_vol_static_create(&flash1_vol, "conf",        0x210000, 64 * 1024, &lv_conf);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_conf, "conf");

	flash_vol_static_create(&flash1_vol, "calib",       0x220000, 64 * 1024, &lv_calib);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_calib, "calib");

	flash_vol_static_create(&flash1_vol, "conf-backup", 0x230000, 64 * 1024, &lv_conf_backup);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_conf_backup, "conf-backup");

	flash_vol_static_create(&flash1_vol, "log",         0x240000, 256 * 1024, &lv_log);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_log, "log");
}


/**********************************************************************************************************************
 * Internal flash partitions (bootloader and application image)
 **********************************************************************************************************************/

static void port_setup_iflash(void) {
	/* The internal flash is memory mapped and executes in place, so both the bootloader and the application
	 * image live here. The QSPI flash1 only stages the update image, which the bootloader flashes into the
	 * "app" partition below. */
	stm32_flash_init(&iflash);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)&iflash.flash, "flash0");

	/* Layout (offset -> size):
	 *   0x000000  bootloader  112K
	 *   0x01c000  bootconf      8K   (bootloader configuration)
	 *   0x01e000  mib           8K   (manufacturing information block)
	 *   0x020000  app         512K   (application image, load address 0x08020000)
	 */
	flash_vol_static_init(&iflash_vol, &iflash.flash);

	flash_vol_static_create(&iflash_vol, "bootloader", 0,          112 * 1024, &lv_bootloader);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_bootloader, "bootloader");

	flash_vol_static_create(&iflash_vol, "bootconf",   112 * 1024, 8 * 1024,   &lv_bootconf);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_bootconf, "bootconf");

	flash_vol_static_create(&iflash_vol, "mib",        120 * 1024, 8 * 1024,   &lv_mib);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_mib, "mib");

	flash_vol_static_create(&iflash_vol, "app",        128 * 1024, 512 * 1024, &lv_app);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_app, "app");

	#if !defined(CONFIG_APP_BL)
		const struct flash_cbor_mib_conf mib_conf = {
			.flash = lv_mib,
			.offset = 0,
			.max_size = 0,
			.public_key_b64 = CONFIG_PORT_NWDAQ_MAIN_HH1_MIB_PUBKEY,
			.root_name = "mib",
		};
		if (flash_cbor_mib_init(&mib, &mib_conf) == FLASH_CBOR_MIB_RET_OK) {
			Conf *mib_root = NULL;
			flash_cbor_mib_get_root(&mib, &mib_root);
			iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CONF, (Interface *)mib_root, "mib");
		}
	#endif
}


/**********************************************************************************************************************
 * System settings and statistics
 **********************************************************************************************************************/

SystemConf system_conf;

static void port_setup_system_conf(void) {
	system_conf_init(&system_conf);

	Conf *system_conf_root = NULL;
	system_conf_get_conf(&system_conf, &system_conf_root);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CONF, (Interface *)system_conf_root, "system");
}


/**********************************************************************************************************************
 * I2C1 bus
 **********************************************************************************************************************/

Stm32I2c i2c1;

Gpio *i2c1_scl = &(gpiob.pin[8]);
Gpio *i2c1_sda = &(gpiob.pin[7]);

static void port_setup_i2c(void) {
	/* Unstuck any slave that may still be holding the bus from before a firmware reset, while the pins are
	 * still plain GPIOs (not yet muxed to the I2C peripheral). */
	stm32_i2c_recovery(i2c1_sda, i2c1_scl);

	/* I2C1 SCL on PB8, SDA on PB7, AF4, open-drain. */
	i2c1_scl->vmt->set_mode(i2c1_scl, MODE_ALTERNATE);
	i2c1_scl->vmt->set_otype(i2c1_scl, OTYPE_OD);
	i2c1_scl->vmt->set_pinmux(i2c1_scl, 4);

	i2c1_sda->vmt->set_mode(i2c1_sda, MODE_ALTERNATE);
	i2c1_sda->vmt->set_otype(i2c1_sda, OTYPE_OD);
	i2c1_sda->vmt->set_pinmux(i2c1_sda, 4);

	/* Configure and run the I2C driver and peripheral when GPIO is ready. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
	NVIC_EnableIRQ(I2C1_EV_IRQn);
	NVIC_SetPriority(I2C1_EV_IRQn, 7);
	NVIC_EnableIRQ(I2C1_ER_IRQn);
	NVIC_SetPriority(I2C1_ER_IRQn, 7);
	stm32_i2c_init(&i2c1, (void *)I2C1);
	stm32_i2c_set_speed(&i2c1, 100000);

	stm32_i2c_scan(&i2c1);
}


void i2c1_ev_isr(void);
void i2c1_ev_isr(void) {
	stm32_i2c_irq_handler(&i2c1);
}


void i2c1_er_isr(void);
void i2c1_er_isr(void) {
	stm32_i2c_irq_handler(&i2c1);
}


/**********************************************************************************************************************
 * I2C2 bus
 **********************************************************************************************************************/

#if !defined(CONFIG_APP_BL)

Stm32I2c i2c2;

Gpio *i2c2_scl = &(gpiob.pin[10]);
Gpio *i2c2_sda = &(gpiob.pin[14]);

static void port_setup_pm_i2c(void) {
	/* Unstuck any slave that may still be holding the bus from before a firmware reset, while the pins are
	 * still plain GPIOs (not yet muxed to the I2C peripheral). */
	stm32_i2c_recovery(i2c2_sda, i2c2_scl);

	/* I2C2 SCL on PB10, SDA on PB14, AF4, open-drain. The board has no external pull-ups on this bus, so the
	 * internal ones are enabled; they are weak, so keep the bus short and the clock slow. */
	i2c2_scl->vmt->set_mode(i2c2_scl, MODE_ALTERNATE);
	i2c2_scl->vmt->set_otype(i2c2_scl, OTYPE_OD);
	i2c2_scl->vmt->set_pull(i2c2_scl, PULL_UP);
	i2c2_scl->vmt->set_pinmux(i2c2_scl, 4);

	i2c2_sda->vmt->set_mode(i2c2_sda, MODE_ALTERNATE);
	i2c2_sda->vmt->set_otype(i2c2_sda, OTYPE_OD);
	i2c2_sda->vmt->set_pull(i2c2_sda, PULL_UP);
	i2c2_sda->vmt->set_pinmux(i2c2_sda, 4);

	/* Configure and run the I2C driver and peripheral when GPIO is ready. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_I2C2EN;
	NVIC_EnableIRQ(I2C2_EV_IRQn);
	NVIC_SetPriority(I2C2_EV_IRQn, 7);
	NVIC_EnableIRQ(I2C2_ER_IRQn);
	NVIC_SetPriority(I2C2_ER_IRQn, 7);
	stm32_i2c_init(&i2c2, (void *)I2C2);
	stm32_i2c_set_speed(&i2c2, 100000);

	//while (true) {
		stm32_i2c_scan(&i2c2);
		//vTaskDelay(100);
	//}
}


void i2c2_ev_isr(void);
void i2c2_ev_isr(void) {
	stm32_i2c_irq_handler(&i2c2);
}


void i2c2_er_isr(void);
void i2c2_er_isr(void) {
	stm32_i2c_irq_handler(&i2c2);
}

#endif


/**********************************************************************************************************************
 * Status RGB LEDs driven by a LP5812
 **********************************************************************************************************************/

Lp581x lp5812;
GpioLed led_bat;
GpioLed led_sys;
GpioLed led_mem;
GpioLed led_ble_wifi;

/* Top LEDs driven by a LP5862 LED matrix driver. Its 36 LED dots drive twelve RGB LEDs. */
Lp586x lp5862;
GpioLed top_led[12];

Gpio *top_led_en_gpio = &(gpiod.pin[11]);

/* Wire a single RGB LED to three consecutive LP5812 PWM channels (red, green, blue). */
static lp581x_ret_t led_setup(GpioLed *led, size_t ch_base) {
	Pwm *r = NULL;
	Pwm *g = NULL;
	Pwm *b = NULL;
	if (lp581x_get_pwm(&lp5812, ch_base + 0, &r) != LP581X_RET_OK ||
	    lp581x_get_pwm(&lp5812, ch_base + 1, &g) != LP581X_RET_OK ||
	    lp581x_get_pwm(&lp5812, ch_base + 2, &b) != LP581X_RET_OK) {
		return LP581X_RET_FAILED;
	}

	gpio_led_init(led, NULL, NULL, NULL);
	gpio_led_set_pwm(led, r, g, b);
	return LP581X_RET_OK;
}

/* Wire a single top RGB LED to three consecutive LP5862 PWM channels (red, green, blue). */
static lp586x_ret_t top_led_setup(GpioLed *led, size_t ch_base) {
	Pwm *r = NULL;
	Pwm *g = NULL;
	Pwm *b = NULL;
	if (lp586x_get_pwm(&lp5862, ch_base + 0, &r) != LP586X_RET_OK ||
	    lp586x_get_pwm(&lp5862, ch_base + 1, &g) != LP586X_RET_OK ||
	    lp586x_get_pwm(&lp5862, ch_base + 2, &b) != LP586X_RET_OK) {
		return LP586X_RET_FAILED;
	}

	gpio_led_init(led, NULL, NULL, NULL);
	/* Inverted RGB! */
	gpio_led_set_pwm(led, b, g, r);
	return LP586X_RET_OK;
}

#define LED_SEQ_GREEN_BLINK LED_SEQ { \
	LED_SEQ_SET | LED_SEQ_RGB(0x00, 0xff, 0x22) | LED_SEQ_TIME_MS(256), \
	LED_SEQ_SET | LED_SEQ_OFF | LED_SEQ_TIME_MS(256), \
	LED_SEQ_END \
} \

#define LED_SEQ_ORANGE_FAST_BLINK LED_SEQ { \
	LED_SEQ_SET | LED_SEQ_RGB(255, 64, 0) | LED_SEQ_TIME_MS(128), \
	LED_SEQ_SET | LED_SEQ_OFF | LED_SEQ_TIME_MS(128), \
	LED_SEQ_END \
} \

/* A short "breathing" effect in white. There is no hardware fade, so the ramp up and down is approximated by a
 * handful of discrete SET steps held for a few tens of milliseconds each, followed by a longer dark pause that
 * marks the gap between breaths. */
#define LED_SEQ_WHITE_BREATHE LED_SEQ { \
	LED_SEQ_SET | LED_SEQ_BRIGHTNESS(0x20) | LED_SEQ_TIME_MS(64), \
	LED_SEQ_SET | LED_SEQ_BRIGHTNESS(0x50) | LED_SEQ_TIME_MS(64), \
	LED_SEQ_SET | LED_SEQ_BRIGHTNESS(0x90) | LED_SEQ_TIME_MS(64), \
	LED_SEQ_SET | LED_SEQ_BRIGHTNESS(0xc0) | LED_SEQ_TIME_MS(64), \
	LED_SEQ_SET | LED_SEQ_BRIGHTNESS(0xff) | LED_SEQ_TIME_MS(96), \
	LED_SEQ_SET | LED_SEQ_BRIGHTNESS(0xc0) | LED_SEQ_TIME_MS(64), \
	LED_SEQ_SET | LED_SEQ_BRIGHTNESS(0x90) | LED_SEQ_TIME_MS(64), \
	LED_SEQ_SET | LED_SEQ_BRIGHTNESS(0x50) | LED_SEQ_TIME_MS(64), \
	LED_SEQ_SET | LED_SEQ_BRIGHTNESS(0x20) | LED_SEQ_TIME_MS(64), \
	/* ~4 s dark pause between breaths. A single step tops out at 1008 ms (6-bit field), so it is split. */ \
	LED_SEQ_SET | LED_SEQ_OFF | LED_SEQ_TIME_MS(1008), \
	LED_SEQ_WAIT | LED_SEQ_TIME_MS(1008), \
	LED_SEQ_WAIT | LED_SEQ_TIME_MS(1008), \
	LED_SEQ_WAIT | LED_SEQ_TIME_MS(976), \
	LED_SEQ_END \
} \

static void port_setup_leds(void) {
	/* C variant of the LP5812 (chip address 0x16). Its 12 PWM channels drive four RGB LEDs. */
	if (lp581x_init(&lp5812, &i2c1.bus, 0x16, LP581X_TYPE_LP5812) != LP581X_RET_OK) {
		return;
	}

	for (size_t i = 0; i < 12; i++) {
		lp581x_set_max_current(&lp5812, i, 0.5f);
	}

	if (led_setup(&led_sys, 0) != LP581X_RET_OK ||
	    led_setup(&led_bat, 3) != LP581X_RET_OK ||
	    led_setup(&led_mem, 6) != LP581X_RET_OK ||
	    led_setup(&led_ble_wifi, 9) != LP581X_RET_OK) {
		return;
	}

	/* Advertise the battery status LED so the application can reflect the battery current on it. */
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_LED, (Interface *)&led_bat.led, "led_bat");

#if defined(CONFIG_APP_BL)
	led_sys.led.vmt->sequence(&led_sys.led, LED_SEQ_ORANGE_FAST_BLINK);
#else
	led_sys.led.vmt->set(&led_sys.led, LED_COLOR_RGB(0, 255, 15));
#endif
}


static void port_setup_top_leds(void) {
	/* Top LEDs driven by a LP5862. EN on PD11 gates the controller, so power it up before talking to it. */
	top_led_en_gpio->vmt->set_mode(top_led_en_gpio, MODE_OUTPUT);
	top_led_en_gpio->vmt->set(top_led_en_gpio, false);
	vTaskDelay(2);
	top_led_en_gpio->vmt->set(top_led_en_gpio, true);
	vTaskDelay(2);

	/* Both ADDR pins low select chip address 0x10. Its 36 LED dots drive twelve RGB LEDs. */
	const struct lp586x_conf lp5862_conf = {
		.i2c = &i2c1.bus,
		.addr = 0x10,
		.type = LP586X_TYPE_LP5862,
	};
	if (lp586x_init(&lp5862, &lp5862_conf) != LP586X_RET_OK) {
		return;
	}

	for (size_t i = 0; i < 36; i++) {
		lp586x_set_max_current(&lp5862, i, 0.5f);
	}

	for (size_t i = 0; i < 12; i++) {
		if (top_led_setup(&top_led[i], i * 3) != LP586X_RET_OK) {
			return;
		}
		top_led[i].led.vmt->set(&(top_led[i].led), LED_COLOR_RGB(0, 0, 0));
	}

	/* Advertised order maps led0..led11 to physical LEDs through this table. Adjust the ordering by hand. */
	GpioLed *const top_led_advert[12] = {
		&top_led[8], &top_led[7], &top_led[6],
		&top_led[9], &top_led[10], &top_led[11],
		&top_led[5], &top_led[4], &top_led[3],
		&top_led[0], &top_led[1], &top_led[2],
	};

	/* Advertise the twelve top LEDs as led0..led11. The locator keeps the name pointer, so the names live in
	 * a static buffer that outlives this function. */
	static char top_led_name[12][8];
	for (size_t i = 0; i < 12; i++) {
		snprintf(top_led_name[i], sizeof(top_led_name[i]), "led%u", (unsigned)i);
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_LED, (Interface *)&top_led_advert[i]->led, top_led_name[i]);
	}

	//top_led[1].led.vmt->set(&(top_led[1].led), LED_COLOR_RGB(0, 255, 31));
	//top_led[3].led.vmt->set(&(top_led[3].led), LED_COLOR_RGB(255, 0, 31));



}


/**********************************************************************************************************************
 * LCD backlight driven by a LP5810
 **********************************************************************************************************************/

Lp581x lp5810;

static void port_setup_lcd(void) {
	/* A variant of the LP5810 (chip address 0x14). Its four outputs drive the LCD backlight LEDs, which are
	 * all wired in parallel. */
	if (lp581x_init(&lp5810, &i2c1.bus, 0x14, LP581X_TYPE_LP5810) != LP581X_RET_OK) {
		return;
	}

	/* Set the max allowed current to maximum. PWM is manipulated in power manager callbacks. */
	for (size_t i = 0; i < 4; i++) {
		lp581x_set_max_current(&lp5810, i, 1.0f);

		Pwm *pwm = NULL;
		if (lp581x_get_pwm(&lp5810, i, &pwm) != LP581X_RET_OK) {
			return;
		}
		pwm->vmt->set_pwm(pwm, 0.0f);
	}
}


/**********************************************************************************************************************
 * ST7586 LCD on SPI2
 **********************************************************************************************************************/

Stm32SpiBus spi2;
Stm32SpiDev spi2_lcd;
LcdSt7586 lcd;

Gpio *spi2_sck  = &(gpiod.pin[1]);
Gpio *spi2_miso = &(gpiod.pin[3]);
Gpio *spi2_mosi = &(gpiod.pin[4]);
Gpio *lcd_reset = &(gpioc.pin[7]);
Gpio *lcd_cs    = &(gpioc.pin[6]);
Gpio *lcd_cd    = &(gpioa.pin[8]);

static void port_setup_display(void) {
	/* SPI2 SCK on PD1, MISO on PD3, MOSI on PD4, AF5. */
	spi2_sck->vmt->set_mode(spi2_sck, MODE_ALTERNATE);
	spi2_sck->vmt->set_pinmux(spi2_sck, 5);
	spi2_sck->vmt->set_ospeed(spi2_sck, OSPEED_VERYHIGH);
	spi2_miso->vmt->set_mode(spi2_miso, MODE_ALTERNATE);
	spi2_miso->vmt->set_pinmux(spi2_miso, 5);
	spi2_miso->vmt->set_ospeed(spi2_miso, OSPEED_VERYHIGH);
	spi2_mosi->vmt->set_mode(spi2_mosi, MODE_ALTERNATE);
	spi2_mosi->vmt->set_pinmux(spi2_mosi, 5);
	spi2_mosi->vmt->set_ospeed(spi2_mosi, OSPEED_VERYHIGH);

	/* Configure and run the SPI bus and the LCD chip-select device. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
	stm32_spibus_init(&spi2, (void *)SPI2, STM32_SPI_PER_TYPE_SPI);
	spi2.bus.vmt->set_sck_freq(&spi2.bus, 16e6);
	spi2.bus.vmt->set_mode(&spi2.bus, 0, 0);
	stm32_spidev_init(&spi2_lcd, &spi2.bus, lcd_cs);

	/* LCD reset on PC7, command/data on PA8. */
	lcd_st7586_init(&lcd, &spi2_lcd.dev, lcd_reset, lcd_cd);
	lcd_st7586_set_contrast(&lcd, 0.30f);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FB, (Interface *)&lcd.fb, "lcd");
}


/**********************************************************************************************************************
 * NCN26010 10Base-T1S ethernet on SPI2
 **********************************************************************************************************************/

#if !defined(CONFIG_APP_BL)

Stm32SpiDev spi2_t1s;
Ncn26010 t1s;

Gpio *t1s_cs  = &(gpiod.pin[0]);
Gpio *t1s_irq = &(gpiod.pin[15]);

static void port_setup_t1s(void) {
	/* IRQ on PD15 is an open-drain output of the NCN26010, pulled up on the MCU side. */
	t1s_irq->vmt->set_mode(t1s_irq, MODE_INPUT);
	t1s_irq->vmt->set_pull(t1s_irq, PULL_UP);

	/* The NCN26010 shares SPI2 with the LCD, so only a new chip-select device is needed. CS on PD0. */
	stm32_spidev_init(&spi2_t1s, &spi2.bus, t1s_cs);

	ncn26010_init(&t1s, &spi2_t1s.dev);
	ncn26010_sleep(&t1s, true);
}


/**********************************************************************************************************************
 * BLE/WIFI module power control
 **********************************************************************************************************************/

Stm32SpiBus spi1;
Stm32SpiDev spi1_ble;
St67w611 ble;

Gpio *ble_sck   = &(gpioa.pin[5]);
Gpio *ble_miso  = &(gpioa.pin[6]);
Gpio *ble_mosi  = &(gpioa.pin[7]);
Gpio *ble_cs    = &(gpioa.pin[4]);
Gpio *ble_en_gpio   = &(gpiob.pin[4]);
Gpio *ble_boot_gpio = &(gpiob.pin[6]);
Gpio *ble_rdy_gpio  = &(gpiod.pin[7]);

static void port_setup_ble(void) {
	/* SPI1 SCK on PA5, MISO on PA6, MOSI on PA7, AF5. */
	ble_sck->vmt->set_mode(ble_sck, MODE_ALTERNATE);
	ble_sck->vmt->set_pinmux(ble_sck, 5);
	ble_sck->vmt->set_ospeed(ble_sck, OSPEED_VERYHIGH);
	ble_miso->vmt->set_mode(ble_miso, MODE_ALTERNATE);
	ble_miso->vmt->set_pinmux(ble_miso, 5);
	ble_miso->vmt->set_ospeed(ble_miso, OSPEED_VERYHIGH);
	ble_mosi->vmt->set_mode(ble_mosi, MODE_ALTERNATE);
	ble_mosi->vmt->set_pinmux(ble_mosi, 5);
	ble_mosi->vmt->set_ospeed(ble_mosi, OSPEED_VERYHIGH);

	/* EN on PB4 powers the module (active high), BOOT on PB6 selects the boot source and RDY on PD7
	 * is the module's SPI handshake output. The driver drives EN/BOOT during its probe. RDY is pulled
	 * down so it reads low while the module is still booting and has not yet driven the line. */
	ble_en_gpio->vmt->set_mode(ble_en_gpio, MODE_OUTPUT);
	ble_en_gpio->vmt->set(ble_en_gpio, false);
	ble_boot_gpio->vmt->set_mode(ble_boot_gpio, MODE_OUTPUT);
	ble_boot_gpio->vmt->set(ble_boot_gpio, false);
	ble_rdy_gpio->vmt->set_mode(ble_rdy_gpio, MODE_INPUT);
	ble_rdy_gpio->vmt->set_pull(ble_rdy_gpio, PULL_DOWN);

	/* Configure and run the SPI bus with the module's chip-select device. CS on PA4, mode 0 (CPOL=0,
	 * CPHA=0), MSB first. */
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
	stm32_spibus_init(&spi1, (void *)SPI1, STM32_SPI_PER_TYPE_SPI);
	spi1.bus.vmt->set_sck_freq(&spi1.bus, 2e6);
	spi1.bus.vmt->set_mode(&spi1.bus, 0, 0);
	stm32_spidev_init(&spi1_ble, &spi1.bus, ble_cs);
	/* The ST67W611M1 NCP protocol uses an active-high (inverted) chip-select. */
	stm32_spidev_set_cs_inverted(&spi1_ble, true);

	/* The module is also wired to USART2 (TX on PD5, RX on PD6, AF7), left unconfigured. Per ST, the
	 * module's UART is a boot/flash/debug-only channel: "AT commands are not passed on UART as they
	 * are only available on SPI". So UART cannot carry the NCP protocol and is not an alternative
	 * transport to the SPI link above; do not try to drive the module over it. */

	const struct st67w611_conf ble_conf = {
		.spidev = &spi1_ble.dev,
		.en_gpio = ble_en_gpio,
		.boot_gpio = ble_boot_gpio,
		.rdy_gpio = ble_rdy_gpio,
	};
	if (st67w611_init(&ble, &ble_conf) != ST67W611_RET_OK) {
		return;
	}

	/* Require every peer to pair on connect: the driver starts pairing automatically and drops any peer
	 * whose pairing fails. This is a driver-specific policy not reachable through the generic Ble interface,
	 * so it stays here with the driver instance. */
	st67w611_set_conn_security(&ble, ST67W611_CONN_SEC_PAIR_ON_CONNECT);

	/* Advertise the generic Ble interface. The application discovers it and builds the GATT server, sets up
	 * security and starts advertising on top of it. */
	Ble *ble_iface = NULL;
	st67w611_get_ble(&ble, &ble_iface);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_BLE, (Interface *)ble_iface, "ble");
}


/**********************************************************************************************************************
 * BQ25798 Li-ion battery charger on the power management I2C bus
 **********************************************************************************************************************/

Bq25798 charger;

Gpio *charge_en_gpio = &(gpioa.pin[2]);

static void port_setup_charger(void) {
	/* CE (charge enable) on PA2 is active low. Drive it low to unblock charging; the charging
	 * itself is then controlled over I2C. */
	charge_en_gpio->vmt->set_mode(charge_en_gpio, MODE_OUTPUT);
	charge_en_gpio->vmt->set(charge_en_gpio, false);

	if (bq25798_init(&charger, &i2c2.bus, BQ25798_I2C_ADDR) != BQ25798_RET_OK) {
		return;
	}

	/* Advertise the battery voltage measurement as a sensor. */
	Sensor *vbat = NULL;
	bq25798_get_battery_voltage(&charger, &vbat);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)vbat, "charger_vbat");

	/* Advertise the battery charging control as a power device. */
	Power *power = NULL;
	bq25798_get_power(&charger, &power);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_POWER, (Interface *)power, "charger");
}


/**********************************************************************************************************************
 * VBUS_LP boost regulator feeding the measurement card
 **********************************************************************************************************************/

Gpio *vbus_lp_en_gpio = &(gpiod.pin[12]);

static void port_setup_vbus_lp(void) {
	/* EN on PD12 is active high. Drive it high to enable the boost regulator that powers the measurement
	 * card. The power manager turns it off again in D3 (and a wake reboots, so this runs fresh on every
	 * boot). */
	vbus_lp_en_gpio->vmt->set_mode(vbus_lp_en_gpio, MODE_OUTPUT);
	vbus_lp_en_gpio->vmt->set(vbus_lp_en_gpio, true);
}


/**********************************************************************************************************************
 * BQ27441-G1 battery fuel gauge on the power management I2C bus
 **********************************************************************************************************************/

Bq27441 fuel_gauge;

static void port_setup_fuel_gauge(void) {
	if (bq27441_init(&fuel_gauge, &i2c2.bus, BQ27441_I2C_ADDR) != BQ27441_RET_OK) {
		return;
	}

	/* Advertise the fuel gauge measurements as sensors for use in applications. */
	Sensor *sensor = NULL;
	bq27441_get_battery_voltage(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_voltage");

	bq27441_get_battery_current(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_current");

	bq27441_get_state_of_charge(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_soc");

	bq27441_get_state_of_health(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_soh");

	bq27441_get_remaining_capacity(&fuel_gauge, &sensor);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)sensor, "bat_remaining");
}

#endif


/**********************************************************************************************************************
 * Keypad using a PCAL6408A GPIO expander on I2C1
 **********************************************************************************************************************/

Pcal6408A keypad_gpio;
GpioKeypad keypad;
KeypadLayout keypad_layout;

/* The gpio-keypad driver only reports raw per-pin presses (EV_RAW_x, one per expander pin). The buttons short
 * the expander input to ground, hence the inputs are pulled up and the sense is inverted. */
struct gpio_keypad_key keypad_keys[] = {
	{ .input = &(keypad_gpio.pin[0]), .type = EV_TYPE_RAW, .code = EV_RAW_0, .invert = true },
	{ .input = &(keypad_gpio.pin[1]), .type = EV_TYPE_RAW, .code = EV_RAW_1, .invert = true },
	{ .input = &(keypad_gpio.pin[2]), .type = EV_TYPE_RAW, .code = EV_RAW_2, .invert = true },
	{ .input = &(keypad_gpio.pin[3]), .type = EV_TYPE_RAW, .code = EV_RAW_3, .invert = true },
	{ .input = &(keypad_gpio.pin[4]), .type = EV_TYPE_RAW, .code = EV_RAW_4, .invert = true },
	{ .input = &(keypad_gpio.pin[5]), .type = EV_TYPE_RAW, .code = EV_RAW_5, .invert = true },
	{ .input = &(keypad_gpio.pin[6]), .type = EV_TYPE_RAW, .code = EV_RAW_6, .invert = true },
	{ .input = &(keypad_gpio.pin[7]), .type = EV_TYPE_RAW, .code = EV_RAW_7, .invert = true },
	{ .input = NULL }
};

/* Layout mapping the raw keypad presses to logical key events. Pin-to-key assignment is provisional and will be
 * corrected against the real hardware later. "OK" has no dedicated event code, so it is mapped to EV_KEY_ENTER. */
const struct keypad_layout_item keypad_layout_map[] = {
	{ .in_code = EV_RAW_0, .out_code = EV_KEY_F3 },
	{ .in_code = EV_RAW_1, .out_code = EV_KEY_F4 },
	{ .in_code = EV_RAW_2, .out_code = EV_KEY_RIGHT },
	{ .in_code = EV_RAW_2, .out_code = EV_REL_X, .counter = 1 },
	{ .in_code = EV_RAW_3, .out_code = EV_KEY_ENTER },
	{ .in_code = EV_RAW_4, .out_code = EV_KEY_LEFT },
	{ .in_code = EV_RAW_4, .out_code = EV_REL_X, .counter = -1 },
	{ .in_code = EV_RAW_5, .out_code = EV_KEY_ESC, .out_code_very_long = EV_KEY_ONOFF },
	{ .in_code = EV_RAW_6, .out_code = EV_KEY_F1, .out_code_long = EV_KEY_F5 },
	{ .in_code = EV_RAW_7, .out_code = EV_KEY_F2, .out_code_long = EV_KEY_F6 },
	{ .in_code = EV_CODE_NONE }
};


static void port_setup_keypad(void) {
	if (pcal6408a_gpio_init(&keypad_gpio, &i2c1.bus) != PCAL6408A_GPIO_RET_OK) {
		return;
	}

	/* All eight expander pins are wired to the keypad buttons. */
	for (size_t i = 0; i < 8; i++) {
		keypad_gpio.pin[i].vmt->set_mode(&(keypad_gpio.pin[i]), MODE_INPUT);
		keypad_gpio.pin[i].vmt->set_pull(&(keypad_gpio.pin[i]), PULL_UP);
	}

	/* The gpio-keypad produces raw events; the keypad-layout service translates them into the key events
	 * consumed downstream and is the one advertised as the "keypad" event source. */
	gpio_keypad_init(&keypad, keypad_keys);

	const struct keypad_layout_conf keypad_layout_conf = {
		.source = &keypad.event,
		.layout = keypad_layout_map,
		.long_press_ms = 1000,
		.very_long_press_ms = 5000,
	};
	keypad_layout_init(&keypad_layout, &keypad_layout_conf);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_EVENT, (Interface *)&keypad_layout.event, "keypad");
}


/**********************************************************************************************************************
 * Piezo beeper driven by TIM1 CH1
 **********************************************************************************************************************/

Stm32Timer beeper_timer;
PwmBeeper beeper;

Gpio *beeper_gpio = &(gpioe.pin[9]);

static void port_setup_beeper(void) {
	/* TIM1_CH1 output on PE9, AF1. */
	beeper_gpio->vmt->set_mode(beeper_gpio, MODE_ALTERNATE);
	beeper_gpio->vmt->set_pinmux(beeper_gpio, 1);

	/* TIM1 is an advanced-control timer on APB2. */
	RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
	stm32_timer_init(&beeper_timer, (void *)TIM1, SystemCoreClock);

	Pwm *pwm = NULL;
	stm32_timer_pwm_init(&beeper_timer, 1, &pwm);
	pwm_beeper_init(&beeper, pwm);

	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_BEEPER, (Interface *)&beeper.beeper, "beeper");

	/* Give a single beep after boot to indicate the firmware is up. */
	//beeper.beeper.vmt->sequence(&beeper.beeper, BEEPER_SEQ_SINGLE_BEEP);
}


#if !defined(CONFIG_APP_BL)
static void port_setup_applets(void) {
#if defined(CONFIG_APPLET_HELLO_WORLD)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_world, "hello-world");
#endif
#if defined(CONFIG_APPLET_SETTINGS)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&settings, "settings");
#endif
#if defined(CONFIG_APPLET_HELLO_WORLD)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_world, "hello-world");
#endif
#if defined(CONFIG_APPLET_SETTINGS)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&settings, "settings");
#endif
#if defined(CONFIG_APPLET_HELLO_WORLD)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_world, "hello-world");
#endif
#if defined(CONFIG_APPLET_SETTINGS)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&settings, "settings");
#endif
#if defined(CONFIG_APPLET_HELLO_WORLD)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_world, "hello-world");
#endif
#if defined(CONFIG_APPLET_SETTINGS)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&settings, "settings");
#endif
#if defined(CONFIG_APPLET_LIVE_DATA)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&live_data, "live-data");
#endif
#if defined(CONFIG_APPLET_HELLO_WREN)
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_APPLET, (Interface *)&hello_wren, "hello-wren");
#endif
}
#endif


/**********************************************************************************************************************
 * Power management
 **********************************************************************************************************************/

PmGeneric pm;

/* Re-apply the clock-derived rates on the peripherals whose divisors are latched from SYSCLK at
 * configuration time, so they keep their configured bitrate/frequency after the system clock has been
 * scaled between the slow (D3) and fast (D2 and up) configurations. I2C is not reprogrammed: both buses
 * are clocked from the fixed HSI16 and are unaffected by the SYSCLK change. */
static void port_reclock_peripherals(void) {
	/* Console. */
	uart4.uart.vmt->set_bitrate(&uart4.uart, 115200);
	/* ST7586 LCD / NCN26010 SPI bus. */
	spi2.bus.vmt->set_sck_freq(&spi2.bus, 16e6);
#if !defined(CONFIG_APP_BL)
	/* nbus2 backplane. */
	uart1.uart.vmt->set_bitrate(&uart1.uart, 1000000);
	/* ST67W611 BLE/WIFI SPI bus. */
	spi1.bus.vmt->set_sck_freq(&spi1.bus, 2e6);
#endif
}

/* Drive all four LCD backlight LEDs to the same brightness (0.0 off .. 1.0 full). */
static void port_set_lcd_brightness(float brightness) {
	for (size_t i = 0; i < 4; i++) {
		Pwm *pwm = NULL;
		if (lp581x_get_pwm(&lp5810, i, &pwm) != LP581X_RET_OK) {
			return;
		}
		pwm->vmt->set_pwm(pwm, brightness);
	}
}

/* One callback per adjacent transition. They gate the clocks, regulators and peripherals when stepping between
 * neighbouring device power levels: the D2 <-> D3 pair scales the system clock between the full 160 MHz PLL and
 * the 4 MHz slow clock, and every callback also dims the LCD backlight for its level. */

static pm_generic_ret_t pm_d0_to_d1(void *ctx) {
	(void)ctx;

	port_set_lcd_brightness(0.5f);
	return PM_GENERIC_RET_OK;
}


static pm_generic_ret_t pm_d1_to_d2(void *ctx) {
	(void)ctx;

	port_set_lcd_brightness(0.01f);
	return PM_GENERIC_RET_OK;
}


static pm_generic_ret_t pm_d2_to_d3(void *ctx) {
	(void)ctx;

	port_set_lcd_brightness(0.0f);
	lcd_st7586_set_sleep(&lcd, true);

	/* Power down both LP581x driver (LCD backlight) to save power. */
	lp581x_enable(&lp5810, false);

	/* Disable the top LEd controller to save power. */
	top_led_en_gpio->vmt->set(top_led_en_gpio, false);

	/* Breathe the sys LED to signal the low-power sleep state. Its vmt is only set once the LP5812 came up. */
	if (led_sys.led.vmt != NULL) {
		led_sys.led.vmt->sequence(&led_sys.led, LED_SEQ_WHITE_BREATHE);
	}

#if !defined(CONFIG_APP_BL)
	/* Put the Wi-Fi/BLE module to sleep: the driver idles its tasks and powers the module down (CHIP_EN). */
	st67w611_set_sleep(&ble, true);

	/* Power down the measurement card: shut down the nbus2 transceiver (active-high) and turn off the
	 * VBUS_LP boost regulator (active-high enable). */
	nbus2_shdn_gpio->vmt->set(nbus2_shdn_gpio, true);
	vbus_lp_en_gpio->vmt->set(vbus_lp_en_gpio, false);
#endif

	/* Enter the low-power slow clock: SYSCLK back to the 4 MHz MSIS, PLL1 and HSE stopped. HSI16 keeps
	 * running, so I2C stays accessible in D3. Reprogram the clock-derived peripherals for the slow clock. */
	port_shutdown_sysclk();
	port_reclock_peripherals();

	return PM_GENERIC_RET_OK;
}


static pm_generic_ret_t pm_d3_to_d2(void *ctx) {
	(void)ctx;

	/* Waking from D3 is recovered by a full system reboot, which brings the clocks, peripherals and the BLE
	 * module (with its GATT configuration) back to a known-good state far more reliably than a piecemeal
	 * software restore. D2 is the manager's default state, so the system boots straight into D2 and only ever
	 * reaches D3 through a real D2 -> D3 descent; this transition is therefore never taken at boot, only on a
	 * genuine wake. Does not return. */
	NVIC_SystemReset();
	return PM_GENERIC_RET_OK;
}


static pm_generic_ret_t pm_d2_to_d1(void *ctx) {
	(void)ctx;

	port_set_lcd_brightness(0.5f);
	return PM_GENERIC_RET_OK;
}


static pm_generic_ret_t pm_d1_to_d0(void *ctx) {
	(void)ctx;

	port_set_lcd_brightness(1.0f);
	return PM_GENERIC_RET_OK;
}


/* The whole power state machine in a single definition. The states array and the transition table are
 * file-scope compound literals, so they keep static storage and the pointers stay valid. States are
 * ordered by power (D0 highest, D3 lowest); locks pull the system up to the highest-power locked state
 * and it settles back down one level at a time once they are released. Which transitions are possible
 * is defined solely by the transition table below. */
static const struct pm_generic_conf pm_conf = {
	.default_state = PM_STATE_D2,
	.states = (const struct pm_generic_state_conf[]){
		{ .state = PM_STATE_D0, .release_timeout = 15000 },
		{ .state = PM_STATE_D1, .release_timeout = 300000 },
		{ .state = PM_STATE_D2, .release_timeout = 3600000 },
		{ .state = PM_STATE_D3 },
		/* Terminator. */
		{ .state = PM_STATE_NONE },
	},
	.transitions = (const struct pm_generic_transition_conf[]){
		{ .from = PM_STATE_D0, .to = PM_STATE_D1, .callback = pm_d0_to_d1 },
		{ .from = PM_STATE_D1, .to = PM_STATE_D2, .callback = pm_d1_to_d2 },
		{ .from = PM_STATE_D2, .to = PM_STATE_D3, .callback = pm_d2_to_d3 },
		{ .from = PM_STATE_D3, .to = PM_STATE_D2, .callback = pm_d3_to_d2 },
		{ .from = PM_STATE_D2, .to = PM_STATE_D1, .callback = pm_d2_to_d1 },
		{ .from = PM_STATE_D1, .to = PM_STATE_D0, .callback = pm_d1_to_d0 },
		/* Terminator. */
		{ .from = PM_STATE_NONE },
	},
};


static void port_setup_pm(void) {
	/* Power the system on to its running state: bring the clock up to the full 160 MHz, reprogram the
	 * clock-derived peripherals (console, backplane and SPI buses, configured at the slow boot clock above)
	 * for it, and set the LCD backlight. The measurement-card rails and the BLE module are already powered on
	 * by their setup functions. */
	port_setup_sysclk();
	port_reclock_peripherals();
	port_set_lcd_brightness(0.01f);

	if (pm_generic_init(&pm, &pm_conf) != PM_GENERIC_RET_OK) {
		return;
	}

	/* Advertise the power manager interface so services can request and lock power states. */
	Pm *pm_iface = NULL;
	pm_generic_get_pm(&pm, &pm_iface);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_PM, (Interface *)pm_iface, "pm");

	/* Bring the system to full power once at boot. With D2 as the default state this walks the power manager
	 * up through the transition table: D2 -> D1 -> D0. */
	pm_iface->vmt->pm_set_state(pm_iface, PM_STATE_D0);
}


int32_t port_init(void) {
	stm32_gpio_init(&gpioa, (void *)GPIOA_BASE);
	stm32_gpio_init(&gpiob, (void *)GPIOB_BASE);
	stm32_gpio_init(&gpioc, (void *)GPIOC_BASE);
	stm32_gpio_init(&gpiod, (void *)GPIOD_BASE);
	stm32_gpio_init(&gpioe, (void *)GPIOE_BASE);

	port_setup_console();
#if !defined(CONFIG_APP_BL)
	port_setup_nbus2();
#endif
	port_setup_i2c();
#if !defined(CONFIG_APP_BL)
	port_setup_pm_i2c();
#endif
	port_setup_leds();
	port_setup_top_leds();
	port_setup_lcd();
	port_setup_display();
#if !defined(CONFIG_APP_BL)
	port_setup_t1s();
	port_setup_ble();
	port_setup_charger();
	port_setup_vbus_lp();
	port_setup_fuel_gauge();
#endif
	/* The power manager owns the system clock and runs in both the application and the bootloader. It must
	 * come up after the LCD backlight (lp5810) and the clock-derived peripherals its transitions reprogram
	 * (console, backplane and SPI buses) have been initialised. */
	port_setup_pm();
	port_setup_flash();
	port_setup_iflash();
	port_setup_system_conf();
	port_setup_keypad();
	port_setup_beeper();
#if !defined(CONFIG_APP_BL)
	port_setup_applets();
#endif

	return PORT_INIT_OK;
}


/* Configure dedicated timer (TIM6) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {
	RCC->APB1RSTR1 |= RCC_APB1RSTR1_TIM6RST;
	RCC->APB1RSTR1 &= ~RCC_APB1RSTR1_TIM6RST;
	/* The timer should run at 1MHz */
	TIM6->PSC = (SystemCoreClock / 1000000) - 1;
	TIM6->CR1 &= ~TIM_CR1_OPM;
	TIM6->ARR = UINT16_MAX;
	TIM6->CR1 |= TIM_CR1_CEN;
}


uint32_t port_task_timer_get_value(void) {
	return TIM6->CNT;
}
