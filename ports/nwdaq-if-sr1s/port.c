/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-if-sr1s short-range interface with a 10Base-T1S switch
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

/* Low level drivers for the STM32G4 family */
#include <services/stm32-gpio/stm32-gpio.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/stm32-i2c/stm32-i2c.h>
#include <services/pcal6408a-gpio/pcal6408a-gpio.h>
#include <services/sja1105/sja1105.h>

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

Stm32Gpio gpioa;
Stm32Gpio gpiob;
Stm32Gpio gpioc;


int32_t port_early_init(void) {
	SystemCoreClock = 16e6;

	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
	RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

Stm32Uart uart2;
static void console_init(void) {
	RCC->APB1ENR1 |= RCC_APB1ENR1_USART2EN;

	/* USART2 TX on PB3 (routed to the SWO pin of the debug connector), AF7. */
	gpiob.pin[3].vmt->set_mode(&(gpiob.pin[3]), MODE_ALTERNATE);
	gpiob.pin[3].vmt->set_pull(&(gpiob.pin[3]), PULL_UP);
	gpiob.pin[3].vmt->set_pinmux(&(gpiob.pin[3]), 7);

	/* Initialise and configure the UART */
	stm32_uart_init(&uart2, (void *)USART2);
	uart2.uart.vmt->set_bitrate(&uart2.uart, 115200);

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


void vPortSetupTimerInterrupt(void);
void vPortSetupTimerInterrupt(void) {
	/* Initialize systick interrupt for FreeRTOS. */
	NVIC_SetPriority(SysTick_IRQn, 15);
	SysTick->LOAD = 16000UL - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}


static void port_setup_default_gpio(void) {
	/* No board-specific GPIO defaults yet. */
}


/**********************************************************************************************************************
 * Internal flash memory and MIB initialisation
 **********************************************************************************************************************/

Stm32Flash iflash;
FlashVolStatic pv_iflash;
Flash *lv_bl;
Flash *lv_conf;
Flash *lv_mib;
Flash *lv_app;
Flash *lv_update;
#if !defined(CONFIG_APP_BL)
FlashCborMib mib;
#endif

static void port_flash_init(void) {
	stm32_flash_init(&iflash);

	flash_vol_static_init(&pv_iflash, &iflash.flash);
	flash_vol_static_create(&pv_iflash, "bootloader", 0,          60 * 1024,  &lv_bl);
	flash_vol_static_create(&pv_iflash, "bootconf",   60 * 1024,  2 * 1024,   &lv_conf);
	flash_vol_static_create(&pv_iflash, "mib",        62 * 1024,  2 * 1024,   &lv_mib);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_mib, "mib");
	flash_vol_static_create(&pv_iflash, "app",        64 * 1024,  128 * 1024, &lv_app);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_app, "app");
	flash_vol_static_create(&pv_iflash, "update",     192 * 1024, 64 * 1024,  &lv_update);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_update, "update");

	#if !defined(CONFIG_APP_BL)
		const struct flash_cbor_mib_conf mib_conf = {
			.flash = lv_mib,
			.offset = 0,
			.max_size = 0,
			.public_key_b64 = CONFIG_PORT_NWDAQ_IF_SR1S_MIB_PUBKEY,
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
 * I2C2 bus and PCAL6408A GPIO expander
 **********************************************************************************************************************/

Stm32I2c i2c2;
Pcal6408A pcal;

Gpio *i2c2_scl = &(gpioa.pin[9]);
Gpio *i2c2_sda = &(gpioa.pin[8]);

static void port_setup_i2c(void) {
	/* Unstuck any slave that may still be holding the bus from before a firmware reset, while the pins are
	 * still plain GPIOs (not yet muxed to the I2C peripheral). */
	stm32_i2c_recovery(i2c2_sda, i2c2_scl);

	/* I2C2 SCL/SDA on PA8/PA9, AF4, open-drain. */
	i2c2_scl->vmt->set_mode(i2c2_scl, MODE_ALTERNATE);
	i2c2_scl->vmt->set_otype(i2c2_scl, OTYPE_OD);
	i2c2_scl->vmt->set_pinmux(i2c2_scl, 4);

	i2c2_sda->vmt->set_mode(i2c2_sda, MODE_ALTERNATE);
	i2c2_sda->vmt->set_otype(i2c2_sda, OTYPE_OD);
	i2c2_sda->vmt->set_pinmux(i2c2_sda, 4);

	/* Configure and run the I2C driver and peripheral when GPIO is ready. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_I2C2EN;
	NVIC_EnableIRQ(I2C2_EV_IRQn);
	NVIC_SetPriority(I2C2_EV_IRQn, 7);
	NVIC_EnableIRQ(I2C2_ER_IRQn);
	NVIC_SetPriority(I2C2_ER_IRQn, 7);
	stm32_i2c_init(&i2c2, (void *)I2C2);

	if (pcal6408a_gpio_init(&pcal, &i2c2.bus) != PCAL6408A_GPIO_RET_OK) {
		return;
	}
	for (int i = 0; i < 8; i++) {
		pcal.pin[i].vmt->set_mode(&(pcal.pin[i]), MODE_OUTPUT);
		pcal.pin[i].vmt->set(&(pcal.pin[i]), false);
	}
	pcal.pin[0].vmt->set(&(pcal.pin[0]), true);
}


void i2c2_ev_isr(void);
void i2c2_ev_isr(void) {
	stm32_i2c_irq_handler(&i2c2);
}


void i2c2_er_isr(void);
void i2c2_er_isr(void) {
	stm32_i2c_irq_handler(&i2c2);
}


/**********************************************************************************************************************
 * SJA1105 Ethernet switch SPI bus
 **********************************************************************************************************************/

Stm32SpiBus spi2;
Stm32SpiDev spi2_sja1105;
Sja1105 sja1105;

/* Active-low reset lines: ETH_NRST (PA11) drives all PHY transceivers, the SJA1105 switch has its
 * own reset on PA12. */
Gpio *phy_nrst_gpio = &(gpioa.pin[11]);
Gpio *sja1105_nrst_gpio = &(gpioa.pin[12]);

static void port_switch_setup(void) {
	/* Hold both the switch and the PHYs in reset to start (reset lines are active-low). */
	sja1105_nrst_gpio->vmt->set_mode(sja1105_nrst_gpio, MODE_OUTPUT);
	phy_nrst_gpio->vmt->set_mode(phy_nrst_gpio, MODE_OUTPUT);
	sja1105_nrst_gpio->vmt->set(sja1105_nrst_gpio, false);
	phy_nrst_gpio->vmt->set(phy_nrst_gpio, false);
	vTaskDelay(pdMS_TO_TICKS(10));

	/* Release the switch reset before talking to it over SPI. */
	sja1105_nrst_gpio->vmt->set(sja1105_nrst_gpio, true);
	vTaskDelay(pdMS_TO_TICKS(10));

	/* SPI2 SCK/MISO/MOSI on PB13/PB14/PB15, AF5. */
	gpiob.pin[13].vmt->set_mode(&(gpiob.pin[13]), MODE_ALTERNATE);
	gpiob.pin[13].vmt->set_pinmux(&(gpiob.pin[13]), 5);
	gpiob.pin[14].vmt->set_mode(&(gpiob.pin[14]), MODE_ALTERNATE);
	gpiob.pin[14].vmt->set_pinmux(&(gpiob.pin[14]), 5);
	gpiob.pin[15].vmt->set_mode(&(gpiob.pin[15]), MODE_ALTERNATE);
	gpiob.pin[15].vmt->set_pinmux(&(gpiob.pin[15]), 5);

	stm32_spibus_init(&spi2, (void *)SPI2, STM32_SPI_PER_TYPE_SPI);
	spi2.bus.vmt->set_sck_freq(&spi2.bus, 4e6);
	spi2.bus.vmt->set_mode(&spi2.bus, 0, 1);

	/* Chip select on PB12, configured and driven by the SPI device driver. */
	stm32_spidev_init(&spi2_sja1105, &spi2.bus, &(gpiob.pin[12]));

	/* Bring the SJA1105 up as a transparent RMII switch with the switch sourcing the reference clock. */
	if (sja1105_init(&sja1105, &spi2_sja1105.dev) != SJA1105_RET_OK) {
		return;
	}
	if (sja1105_setup_dumb_switch(&sja1105) != SJA1105_RET_OK) {
		return;
	}

	/* The switch now sources the RMII reference clock; release the PHYs from reset. */
	phy_nrst_gpio->vmt->set(phy_nrst_gpio, true);
}


int32_t port_init(void) {
	stm32_gpio_init(&gpioa, (void *)0x48000000);
	stm32_gpio_init(&gpiob, (void *)0x48000400);
	stm32_gpio_init(&gpioc, (void *)0x48000800);

	console_init();
	port_setup_default_gpio();
	port_flash_init();
	port_setup_i2c();
	port_switch_setup();

	/* Front-panel ports 1..4 correspond to switch ports 1..4. Each has a green activity LED on the
	 * PCAL6408A expander: port 1 => LED pin 1, port 2 => pin 3, port 3 => pin 5, port 4 => pin 7.
	 * The LEDs are active-high. */
	static const struct {
		unsigned int switch_port;
		unsigned int led_pin;
	} port_leds[] = {
		{1, 1},
		{2, 3},
		{3, 4},
		{4, 6},
	};
	uint64_t last_frames[4] = {0};

	/* Poll the per-port frame counters and blink each port's green LED while traffic is flowing. */
	while (1) {
		//char s[200] = {};
		for (size_t i = 0; i < 4; i++) {
			struct sja1105_port_counters counters;
			if (sja1105_read_port_counters(&sja1105, port_leds[i].switch_port, &counters) != SJA1105_RET_OK) {
				continue;
			}
			//snprintf(s, sizeof(s), "%s %d: %lu/%lu", s, i, (uint32_t)counters.n_rxframe, (uint32_t)counters.n_txframe);
			uint64_t frames = counters.n_rxframe + counters.n_txframe;
			Gpio *led = &(pcal.pin[port_leds[i].led_pin]);
			if (frames != last_frames[i]) {
				/* RX or TX activity since the last poll: blink by toggling the LED. */
				led->vmt->toggle(led);
			} else {
				/* No traffic: keep the LED off (active-high). */
				led->vmt->set(led, false);
			}
			last_frames[i] = frames;
			vTaskDelay(pdMS_TO_TICKS(20));
		}
		//u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("counters:%s"), s);
	}

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
