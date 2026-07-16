/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-inc5 single-axis inclination sensing module
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

/* Low level drivers for the STM32G4 family */
#include <services/stm32-gpio/stm32-gpio.h>
#include <services/stm32-spi/stm32-spi.h>
#include <services/stm32-uart/stm32-uart.h>
#include <services/adc-mcp3564/mcp3564.h>

#include <services/stm32-flash/stm32-flash.h>
#include <services/flash-vol-static/flash-vol-static.h>
#if !defined(CONFIG_APP_BL)
	#include <services/flash-cbor-mib/flash-cbor-mib.h>
#endif

#include <interfaces/led-sequences.h>
#include <services/gpio-led/gpio-led.h>


/**
 * Port specific global variables and singleton instances.
 */

uint32_t SystemCoreClock;

Stm32Gpio gpioa;
Stm32Gpio gpiob;
Stm32Gpio gpioc;

Gpio *led_error_gpio = &(gpiob.pin[12]);
Gpio *led_stat_gpio = &(gpiob.pin[13]);

/* Forward declarations for ISR handlers wired into the libopencm3 vector table. */
void usart1_isr(void);
void tim4_isr(void);
void tim2_isr(void);


int32_t port_early_init(void) {
	SystemCoreClock = 16e6;

	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
	RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
	RCC->AHB2ENR |= RCC_AHB2ENR_DAC1EN;
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
	RCC->APB1ENR1 |= RCC_APB1ENR1_SPI3EN;

	/* Timer 6 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;

	/* ADC is used for PCB temperature measurement */
	RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;
	/* Select SYSCLK as the ADC12 kernel clock source. */
	RCC->CCIPR |= 3 << 28;

	return PORT_EARLY_INIT_OK;
}


/**********************************************************************************************************************
 * System serial console initialisation
 **********************************************************************************************************************/

/** @todo unused, remove */
Stm32Uart uart1;
static void console_init(void) {
	/* USART1 RX/TX on PB6/PB7, AF7. */
	gpiob.pin[6].vmt->set_mode(&(gpiob.pin[6]), MODE_ALTERNATE);
	gpiob.pin[6].vmt->set_pull(&(gpiob.pin[6]), PULL_UP);
	gpiob.pin[6].vmt->set_pinmux(&(gpiob.pin[6]), 7);
	gpiob.pin[7].vmt->set_mode(&(gpiob.pin[7]), MODE_ALTERNATE);
	gpiob.pin[7].vmt->set_pull(&(gpiob.pin[7]), PULL_UP);
	gpiob.pin[7].vmt->set_pinmux(&(gpiob.pin[7]), 7);

	/* Initialise and configure the UART */
	stm32_uart_init(&uart1, (void *)USART1);
	uart1.uart.vmt->set_bitrate(&uart1.uart, 115200);

	NVIC_EnableIRQ(USART1_IRQn);
	NVIC_SetPriority(USART1_IRQn, 7);

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
	NVIC_SetPriority(SysTick_IRQn, 15);
	SysTick->LOAD = 16000UL - 1;
	SysTick->VAL = 0;
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}


static void port_setup_default_gpio(void) {
	/* MUX selection outputs. */
	gpioa.pin[2].vmt->set_mode(&(gpioa.pin[2]), MODE_OUTPUT);

	/* Excitation outputs. */
	gpioa.pin[4].vmt->set_mode(&(gpioa.pin[4]), MODE_ANALOG);
	gpioa.pin[5].vmt->set_mode(&(gpioa.pin[5]), MODE_ANALOG);

	/* LEDs */
	gpiob.pin[12].vmt->set_mode(&(gpiob.pin[12]), MODE_OUTPUT);
	gpiob.pin[13].vmt->set_mode(&(gpiob.pin[13]), MODE_OUTPUT);
}


/**********************************************************************************************************************
 * Liquid sensor excitation
 **********************************************************************************************************************/

static void liquid_sensor_init(void) {

	/* Enable excitation. The two DAC channels output static levels of 0.35 and 0.65
	 * of the full scale (12 bit right-aligned). */
	DAC1->DHR12R1 = (uint16_t)(0.35f * 4095.0f);
	DAC1->DHR12R2 = (uint16_t)(0.65f * 4095.0f);
	DAC1->CR |= DAC_CR_EN1;
	DAC1->CR |= DAC_CR_EN2;

	RCC->APB1ENR1 |= RCC_APB1ENR1_TIM4EN;

	TIM4->CR1 &= ~TIM_CR1_CEN;
	/* Edge-aligned, up-counting, no clock division. */
	TIM4->CR1 &= ~(TIM_CR1_CKD | TIM_CR1_CMS | TIM_CR1_DIR);
	TIM4->CR1 &= ~TIM_CR1_ARPE;
	TIM4->PSC = 8 - 1;
	TIM4->CR1 &= ~TIM_CR1_OPM;
	TIM4->ARR = 2500 - 1;

	/* OC3 and OC4 are used purely as compare-match time references for the measurement
	 * sequence; the output stages stay disabled. */
	TIM4->CCER &= ~TIM_CCER_CC3E;
	TIM4->CCMR2 &= ~TIM_CCMR2_OC3PE;
	TIM4->CCMR2 &= ~TIM_CCMR2_OC3M;
	TIM4->CCMR2 |= TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2;
	TIM4->CCR3 = 1000 - 1;
	TIM4->DIER |= TIM_DIER_CC3IE;

	TIM4->CCER &= ~TIM_CCER_CC4E;
	TIM4->CCMR2 &= ~TIM_CCMR2_OC4PE;
	TIM4->CCMR2 &= ~TIM_CCMR2_OC4M;
	TIM4->CCMR2 |= TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2;
	TIM4->CCR4 = 2000 - 1;
	TIM4->DIER |= TIM_DIER_CC4IE;

	/* Enable toggling of the excitation outputs. */
	TIM4->DIER |= TIM_DIER_UIE;

	NVIC_EnableIRQ(TIM4_IRQn);
	NVIC_SetPriority(TIM4_IRQn, 5);
	TIM4->CR1 |= TIM_CR1_CEN;

}


/**********************************************************************************************************************
 * Liquid tilt sensor and temp sensor readout using MCP3564
 **********************************************************************************************************************/

Stm32SpiBus spi1;
Stm32SpiDev spi1_adc;
Mcp3564 mcp;
SemaphoreHandle_t adc_meas;
SemaphoreHandle_t adc_ready;
SemaphoreHandle_t adc_new_cycle;

#define MUX_CH_INC_X MCP3564_MUX_CH0
#define MUX_CH_TEMP_X MCP3564_MUX_CH1

const enum mcp3564_mux adc_mux_config[2] = {
	MUX_CH_INC_X,
	MUX_CH_TEMP_X,
};

int32_t adc_value[2];
int32_t adc_avg[2];

volatile uint32_t adc_cycle = 0;


/**********************************************************************************************************************
 * Sensor interface abstraction over the inclination and temperature values
 **********************************************************************************************************************/

static float adc_to_ntc(int32_t adc, float ref) {
	return ref * (adc + 8388608.0f) / (16777215.0f - (adc + 8388608.0f));
}


static float ntc_to_temp(float ntc, float beta, float ref) {
	return 1.0f / (1.0f / (25.0f + 273.15f) + (1.0f / beta) * log(ntc / ref)) - 273.15f;
}


/* A single measured quantity exported as a Sensor interface. The ADC task stores the latest
 * converted value into @p value and gives the @p ready semaphore once per measurement cycle.
 * A reader blocks in value_f on @p ready until the next measurement becomes available. Each
 * sensor has its own semaphore so all of them can be read at the full measurement rate. */
typedef struct {
	Sensor sensor;
	SemaphoreHandle_t ready;
	float value;
} Inc5Sensor;

static sensor_ret_t inc5_sensor_value_f(Sensor *sensor, float *value) {
	Inc5Sensor *self = sensor->parent;

	/* Wait for the next measurement to become available. */
	if (xSemaphoreTake(self->ready, portMAX_DELAY) != pdTRUE) {
		return SENSOR_RET_FAILED;
	}
	if (value != NULL) {
		*value = self->value;
	}
	return SENSOR_RET_OK;
}

static const struct sensor_vmt inc5_sensor_vmt = {
	.value_f = inc5_sensor_value_f,
};

static const struct sensor_info inc_x_sensor_info = {
	.description = "single-axis inclination",
	.unit = "LSB",
};

static const struct sensor_info temp_x_sensor_info = {
	.description = "PCB temperature",
	.unit = "°C",
};

Inc5Sensor inc_x_sensor;
Inc5Sensor temp_x_sensor;

static void inc5_sensor_init(Inc5Sensor *self, const struct sensor_info *info) {
	self->value = 0.0f;
	self->ready = xSemaphoreCreateBinary();
	self->sensor.vmt = &inc5_sensor_vmt;
	self->sensor.info = info;
	self->sensor.parent = self;
}


static void adc_task(void *p) {
	(void)p;

	while (true) {
		uint8_t status = 0;
		int32_t r = 0;

		xSemaphoreTake(adc_new_cycle, portMAX_DELAY);
		led_error_gpio->vmt->set(led_error_gpio, true);
		led_error_gpio->vmt->set(led_error_gpio, false);

		/* Try to take the following semaphores. The reason for this is the first one
		 * which should be signalled is adc_new_cycle but that happens during the update
		 * event which happens last. */

		/* Manage the measurement sequence. */
		mcp3564_set_mux(&mcp, MCP3564_MUX_VCM, adc_mux_config[(adc_cycle % 4) / 2]);

		/* Wait for the right time to start the measurement. */
		xSemaphoreTake(adc_meas, 0);
		xSemaphoreTake(adc_meas, portMAX_DELAY);
		led_error_gpio->vmt->set(led_error_gpio, true);
		led_error_gpio->vmt->set(led_error_gpio, false);
		mcp3564_send_cmd(&mcp, MCP3564_CMD_START, &status, NULL, 0, NULL, 0);
		led_error_gpio->vmt->set(led_error_gpio, true);
		led_error_gpio->vmt->set(led_error_gpio, false);

		/* Wait until data ready arrives. */
		xSemaphoreTake(adc_ready, 0);
		xSemaphoreTake(adc_ready, portMAX_DELAY);
		led_error_gpio->vmt->set(led_error_gpio, true);
		led_error_gpio->vmt->set(led_error_gpio, false);
		mcp3564_read_reg(&mcp, MCP3564_REG_ADCDATA, 4, (uint32_t *)&r, &status);

		/* Process the measurement. */
		if (adc_cycle % 2) {
			adc_value[(adc_cycle % 4) / 2] += r;
		} else {
			adc_value[(adc_cycle % 4) / 2] -= r;
		}

		if ((adc_cycle % 160) == 159) {
			for (size_t i = 0; i < 2; i++) {
				adc_avg[i] = adc_value[i] / 40.0f;
				adc_value[i] = 0;
			}

			/* Convert the raw accumulators and expose them through the Sensor interfaces.
			 * Give each sensor its own semaphore so both can be read at the full rate. */
			inc_x_sensor.value = adc_avg[0] / 256.0f;
			xSemaphoreGive(inc_x_sensor.ready);

			temp_x_sensor.value = ntc_to_temp(adc_to_ntc(adc_avg[1], 10000.0f), 3977.0f, 10000.0f);
			xSemaphoreGive(temp_x_sensor.ready);
		}
		led_error_gpio->vmt->set(led_error_gpio, true);
		led_error_gpio->vmt->set(led_error_gpio, false);
	}
	vTaskDelete(NULL);
}


static void adc_init(void) {
	/* SPI1 on PA6/PA7 and PB3, AF5. */
	gpioa.pin[6].vmt->set_mode(&(gpioa.pin[6]), MODE_ALTERNATE);
	gpioa.pin[6].vmt->set_pinmux(&(gpioa.pin[6]), 5);
	gpioa.pin[7].vmt->set_mode(&(gpioa.pin[7]), MODE_ALTERNATE);
	gpioa.pin[7].vmt->set_pinmux(&(gpioa.pin[7]), 5);
	gpiob.pin[3].vmt->set_mode(&(gpiob.pin[3]), MODE_ALTERNATE);
	gpiob.pin[3].vmt->set_pinmux(&(gpiob.pin[3]), 5);

	stm32_spibus_init(&spi1, (void *)SPI1, STM32_SPI_PER_TYPE_SPI);
	spi1.bus.vmt->set_sck_freq(&spi1.bus, 4e6);
	spi1.bus.vmt->set_mode(&spi1.bus, 0, 0);

	/* Chip select on PC4, configured and driven by the SPI device driver. */
	stm32_spidev_init(&spi1_adc, &spi1.bus, &(gpioc.pin[4]));

	mcp3564_init(&mcp, &(spi1_adc.dev));
	mcp3564_set_stp_enable(&mcp, false);
	mcp3564_set_gain(&mcp, MCP3564_GAIN_1);
	mcp3564_set_osr(&mcp, MCP3564_OSR_128);
	mcp3564_set_mux(&mcp, MCP3564_MUX_VCM, MCP3564_MUX_CH4);
	mcp3564_set_irq_mode(&mcp, MCP3564_IRQ_MODE_IRQ);
	mcp3564_set_stp_enable(&mcp, true);
	mcp3564_update(&mcp);

	adc_meas = xSemaphoreCreateBinary();
	adc_ready = xSemaphoreCreateBinary();
	adc_new_cycle = xSemaphoreCreateBinary();
	xTaskCreate(adc_task, "adc-task", configMINIMAL_STACK_SIZE + 256, NULL, 3, NULL);

}


void tim4_isr(void) {
	if (TIM4->SR & TIM_SR_CC3IF) {
		TIM4->SR &= ~TIM_SR_CC3IF;

		BaseType_t woken = pdFALSE;
		xSemaphoreGiveFromISR(adc_meas, &woken);
		portYIELD_FROM_ISR(woken);
	}

	if (TIM4->SR & TIM_SR_CC4IF) {
		TIM4->SR &= ~TIM_SR_CC4IF;

		BaseType_t woken = pdFALSE;
		xSemaphoreGiveFromISR(adc_ready, &woken);
		portYIELD_FROM_ISR(woken);
	}

	if (TIM4->SR & TIM_SR_UIF) {
		TIM4->SR &= ~TIM_SR_UIF;

		adc_cycle++;

		/* Alternate the excitation outputs. Must happen regardless of the measurement sequence. */
		if (adc_cycle % 2) {
			gpioa.pin[2].vmt->set(&(gpioa.pin[2]), true);
		} else {
			gpioa.pin[2].vmt->set(&(gpioa.pin[2]), false);
		}

		BaseType_t woken = pdFALSE;
		xSemaphoreGiveFromISR(adc_new_cycle, &woken);
		portYIELD_FROM_ISR(woken);
	}



}


/**********************************************************************************************************************
 * Internal flash memory and MIB initialisation
 **********************************************************************************************************************/

Stm32Flash iflash;
FlashVolStatic pv_iflash;
Flash *lv_bl;
Flash *lv_blconf;
Flash *lv_mib;
Flash *lv_app;
Flash *lv_conf;
Flash *lv_update;
#if !defined(CONFIG_APP_BL)
FlashCborMib mib;
#endif

static void port_flash_init(void) {
	stm32_flash_init(&iflash);

	flash_vol_static_init(&pv_iflash, &iflash.flash);
	flash_vol_static_create(&pv_iflash, "bootloader", 0,          60 * 1024,  &lv_bl);
	flash_vol_static_create(&pv_iflash, "bootconf",   60 * 1024,  2 * 1024,   &lv_blconf);
	flash_vol_static_create(&pv_iflash, "mib",        62 * 1024,  2 * 1024,   &lv_mib);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_mib, "mib");
	flash_vol_static_create(&pv_iflash, "app",        64 * 1024,  126 * 1024, &lv_app);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_app, "app");
	flash_vol_static_create(&pv_iflash, "conf",        190 * 1024,  2 * 1024, &lv_conf);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_conf, "conf");
	flash_vol_static_create(&pv_iflash, "update",     192 * 1024, 64 * 1024,  &lv_update);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)lv_update, "update");

	#if !defined(CONFIG_APP_BL)
		const struct flash_cbor_mib_conf mib_conf = {
			.flash = lv_mib,
			.offset = 0,
			.max_size = 0,
			.public_key_b64 = CONFIG_PORT_NWDAQ_INC5_MIB_PUBKEY,
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
 * LED initialization
 **********************************************************************************************************************/

GpioLed led_error;
GpioLed led_stat;

static void led_init(void) {
	gpio_led_init(&led_error, led_error_gpio, NULL, NULL);
	gpio_led_init(&led_stat, led_stat_gpio, NULL, NULL);
	#if defined(CONFIG_APP_BL)
		led_error.led.vmt->sequence(&led_error.led, LED_SEQ_FAST_BLINK);
	#else
		led_stat.led.vmt->sequence(&led_stat.led, LED_SEQ_HEARTBEAT);
	#endif
}


int32_t port_init(void) {
	stm32_gpio_init(&gpioa, (void *)0x48000000);
	stm32_gpio_init(&gpiob, (void *)0x48000400);
	stm32_gpio_init(&gpioc, (void *)0x48000800);

	port_setup_default_gpio();
	console_init();
	led_init();
	port_flash_init();

	#if !defined(CONFIG_APP_BL)
		inc5_sensor_init(&inc_x_sensor, &inc_x_sensor_info);
		inc5_sensor_init(&temp_x_sensor, &temp_x_sensor_info);

		adc_init();
		liquid_sensor_init();

		/* Advertise the inclination and temperature sensors so the application can read them. */
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&inc_x_sensor.sensor, "inc_x");
		iservicelocator_add(locator, ISERVICELOCATOR_TYPE_SENSOR, (Interface *)&temp_x_sensor.sensor, "temp_x");
	#endif

	return PORT_INIT_OK;
}


void tim2_isr(void) {
	if (TIM2->SR & TIM_SR_UIF) {
		TIM2->SR &= ~TIM_SR_UIF;
		// system_clock_overflow_handler(&system_clock);
	}
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
