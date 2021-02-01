#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/pwr.h>
#include <libopencm3/stm32/lptimer.h>

#include "config.h"
#include "FreeRTOS.h"
#include "timers.h"
#include "u_assert.h"
#include "u_log.h"

#include "port.h"
#include "module_led.h"
#include "interface_led.h"
#include "module_usart.h"
#include "interface_stream.h"
#include "interface_spibus.h"
#include "module_spibus_locm3.h"
#include "interface_flash.h"
#include "module_spi_flash.h"
#include "interface_spidev.h"
#include "module_spidev_locm3.h"
#include "interface_rtc.h"
#include "module_rtc_locm3.h"
#include "interface_rng.h"
#include "module_prng_simple.h"
#include "interfaces/adc.h"
#include "interface_mac.h"
#include "interface_profiling.h"
#include "module_fifo_profiler.h"
#include "umesh_l2_status.h"

#include "interfaces/sensor.h"
#include "interfaces/cellular.h"
#include "interfaces/servicelocator.h"
#include "services/plocator/plocator.h"
#include "services/cli/system_cli_tree.h"
#include "services/stm32-system-clock/clock.h"
#include "services/stm32-rtc/rtc.h"


/**
 * Port specific global variables and singleton instances.
 */

/* Old-style HAL */
uint32_t SystemCoreClock;
struct module_led led_stat;
struct module_usart console;
struct module_rtc_locm3 rtc1;
struct module_prng_simple prng;
struct module_fifo_profiler profiler;


#if defined(CONFIG_SERVICE_STM32_WATCHDOG)
	#include "services/stm32-watchdog/watchdog.h"
	Watchdog watchdog;
#endif

PLocator plocator;
IServiceLocator *locator;
SystemClock system_clock;
Stm32Rtc rtc;

#define RCC_CCIPR_LPTIM1SEL_LSE (3 << 18)
volatile uint16_t last_tick;
volatile uint16_t lptim1_ext;


typedef struct {
	uint8_t cs_port;
	uint8_t cs_pin;
	uint8_t spi;
} Icm52688p;

static void write_8(Icm52688p *self, uint8_t addr, uint8_t data) {
	gpio_clear(self->cs_port, self->cs_pin);
	spi_send8(self->spi, addr & 0x7f);
	spi_read8(self->spi);
	spi_send8(self->spi, data);
	spi_read8(self->spi);
	gpio_set(self->cs_port, self->cs_pin);
}


static uint8_t read_8(Icm52688p *self, uint8_t addr) {
	gpio_clear(self->cs_port, self->cs_pin);
	spi_send8(self->spi, addr | 0x80);
	spi_read8(self->spi);
	spi_send8(self->spi, 0x00);
	uint8_t ret = spi_read8(self->spi);
	gpio_set(self->cs_port, self->cs_pin);
	return ret;
}


static void spi_init(void) {
	spi_set_master_mode(SPI1);
	spi_set_baudrate_prescaler(SPI1, SPI_CR1_BR_FPCLK_DIV_2);
	spi_set_clock_polarity_0(SPI1);
	spi_set_clock_phase_0(SPI1);
	spi_set_full_duplex_mode(SPI1);
	spi_set_unidirectional_mode(SPI1);
	spi_enable_software_slave_management(SPI1);
	spi_send_msb_first(SPI1);
	spi_set_nss_high(SPI1);
	spi_fifo_reception_threshold_8bit(SPI1);
	spi_set_data_size(SPI1, SPI_CR2_DS_8BIT);
	spi_enable(SPI1);
}


int32_t port_early_init(void) {
	/* Relocate the vector table first if required. */
	#if defined(CONFIG_RELOCATE_VECTOR_TABLE)
		SCB_VTOR = CONFIG_VECTOR_TABLE_ADDRESS;
	#endif

	#if defined(CONFIG_INCL_G104MF_CLOCK_80MHZ)
		/* placeholder */
	#elif defined(CONFIG_INCL_G104MF_CLOCK_DEFAULT)
		/* Do not intiialize anything, keep MSI clock running. */
		rcc_set_msi_range(RCC_CR_MSIRANGE_4MHZ);
		SystemCoreClock = 4e6;
	#else
		#error "no clock speed defined"
	#endif

	rcc_apb1_frequency = SystemCoreClock;
	rcc_apb2_frequency = SystemCoreClock;

	rcc_periph_clock_enable(RCC_SYSCFG);

	/* PWR peripheral for setting sleep/stop modes. */
	rcc_periph_clock_enable(RCC_PWR);
	PWR_CR1 |= PWR_CR1_DBP;
	RCC_BDCR |= RCC_BDCR_LSEBYP;
	rcc_osc_on(RCC_LSE);
	rcc_wait_for_osc_ready(RCC_LSE);

	/* Adjust MSI freq using LSE. */
	RCC_CR |= RCC_CR_MSIPLLEN;

	/* Enable low power timer for systick and configure it to use LSE (TCXO). */
	RCC_CCIPR |= RCC_CCIPR_LPTIM1SEL_LSE;
	rcc_periph_clock_enable(RCC_LPTIM1);

	/* Initialize all required clocks for GPIO ports. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);
	rcc_periph_clock_enable(RCC_GPIOH);

	/* Configure clocks in stop modes. */
	RCC_AHB1SMENR = 0;
	RCC_AHB2SMENR = 0;
	RCC_AHB3SMENR = 0;
	RCC_APB1SMENR1 = RCC_APB1SMENR1_LPTIM1SMEN;
	RCC_APB1SMENR2 = 0;
	RCC_APB2SMENR = RCC_APB2SMENR_USART1SMEN;

	return PORT_EARLY_INIT_OK;
}


int32_t port_init(void) {
	/* ADXL power off, GPS power off. */
	gpio_mode_setup(GPIOH, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0 | GPIO1);
	gpio_clear(GPIOH, GPIO0 | GPIO1);

	/* ACCEL1_CS */
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13);
	gpio_set(GPIOC, GPIO13);

	/* RFM_CS */
	gpio_mode_setup(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO15);
	gpio_set(GPIOC, GPIO15);

	/* SD_PWR_EN */
	gpio_mode_setup(GPIOH, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO3);
	gpio_clear(GPIOH, GPIO3);

	/* LORA_PWR_EN */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0);
	gpio_clear(GPIOA, GPIO0);

	/* SD_SEL */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO5);
	gpio_set(GPIOA, GPIO5);

	/* MCU_USB_VBUS_DET */
	gpio_mode_setup(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, GPIO4);

	/* QSPI_BK1_NCS */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO11);
	gpio_set(GPIOB, GPIO11);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO3 | GPIO4 | GPIO5);
	gpio_set_af(GPIOB, GPIO_AF5, GPIO3 | GPIO4 | GPIO5);
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO8);
	gpio_set(GPIOB, GPIO8);

	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO11 | GPIO12);

	/* I2C */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO6 | GPIO9);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, GPIO6 | GPIO9);
	gpio_set(GPIOB, GPIO6 | GPIO9);

	/* microSD CS + SPI2 */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12 | GPIO13 | GPIO14 | GPIO15);
	gpio_set(GPIOB, GPIO12 | GPIO13 | GPIO14 | GPIO15);

	/* ACCEL_INT */	
	gpio_mode_setup(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO7);




	/* Serial console. */
	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO9 | GPIO10);
	gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO9 | GPIO10);
	gpio_set_af(GPIOA, GPIO_AF7, GPIO9 | GPIO10);

	/* Main system console is created on the first USART interface. Enable
	 * USART clocks and interrupts and start the corresponding HAL module. */
	rcc_periph_clock_enable(RCC_USART1);
	nvic_enable_irq(NVIC_USART1_IRQ);
	nvic_set_priority(NVIC_USART1_IRQ, 6 * 16);
	module_usart_init(&console, "console", USART1);
	hal_interface_set_name(&(console.iface.descriptor), "console");

	/* Console is now initialized, set its stream to be used as u_log output. */
	u_log_set_stream(&(console.iface));

	/* First, initialize the service locator service. It is required to
	 * advertise any port-specific devices and interfaces. */
	if (plocator_init(&plocator) == PLOCATOR_RET_OK) {
		locator = plocator_iface(&plocator);
	}

	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_STREAM,
		(Interface *)&console.iface.descriptor,
		"console"
	);

	/** @deprecated
	 * Old real-time clock service will be removed. There is
	 * a new clock interface and services providing system and RTC clocks. */
	/* Initialize the Real-time clock. */
	module_rtc_locm3_init(&rtc1, "rtc1");
	hal_interface_set_name(&(rtc1.iface.descriptor), "rtc1");
	u_log_set_rtc(&(rtc1.iface));
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_RTC,
		(Interface *)&rtc1.iface.descriptor,
		"rtc1"
	);

	#if defined(CONFIG_INCL_G104MF_ENABLE_LEDS)
		/* Allow reprogramming before disabling SWD. */
		vTaskDelay(3000);
		
		/* Status LEDs. */
		gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO14);
		module_led_init(&led_stat, "led_stat");
		module_led_set_port(&led_stat, GPIOA, GPIO14);
		interface_led_loop(&(led_stat.iface), 0x11f1);
		hal_interface_set_name(&(led_stat.iface.descriptor), "led_stat");
		iservicelocator_add(
			locator,
			ISERVICELOCATOR_TYPE_LED,
			(Interface *)&led_stat.iface.descriptor,
			"led_stat"
		);
	#endif

	#if defined(CONFIG_INCL_G104MF_ENABLE_WATCHDOG)
		watchdog_init(&watchdog, 20000, 0);
	#endif

	rcc_periph_clock_enable(RCC_TIM2);
	rcc_periph_reset_pulse(RST_TIM2);
	nvic_enable_irq(NVIC_TIM2_IRQ);

	system_clock_init(&system_clock, TIM2, SystemCoreClock / 1000000 - 1, UINT32_MAX);
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_CLOCK,
		&system_clock.iface.interface,
		"main"
	);

	stm32_rtc_init(&rtc);
	iservicelocator_add(
		locator,
		ISERVICELOCATOR_TYPE_CLOCK,
		&rtc.iface.interface,
		"rtc"
	);



	return PORT_INIT_OK;
}


void tim2_isr(void) {
	if (TIM_SR(TIM2) & TIM_SR_UIF) {
		timer_clear_flag(TIM2, TIM_SR_UIF);
		system_clock_overflow_handler(&system_clock);
	}
}


void vPortSetupTimerInterrupt(void) {
	lptimer_set_internal_clock_source(LPTIM1);
	lptimer_enable_trigger(LPTIM1, LPTIM_CFGR_TRIGEN_SW);
	lptimer_set_prescaler(LPTIM1, LPTIM_CFGR_PRESC_1);
	lptimer_enable(LPTIM1);
	lptimer_set_period(LPTIM1, 0xffff);
	lptimer_set_compare(LPTIM1, 0 + 31);
	lptimer_enable_irq(LPTIM1, LPTIM_IER_CMPMIE);
	nvic_enable_irq(NVIC_LPTIM1_IRQ);
	lptimer_start_counter(LPTIM1, LPTIM_CR_CNTSTRT);
}

void port_task_timer_init(void) {
	/* LPTIM1 us used for task statistics. It is already initialized. */
}


uint32_t port_task_timer_get_value(void) {
	return lptim_get_extended();
}


void usart1_isr(void) {
	module_usart_interrupt_handler(&console);
}


void vApplicationIdleHook(void);
void vApplicationIdleHook(void) {



	/* Enter STOP 1 mode. Set LPMS to 001, SLEEPDEEP bit and do a WFI. */
	SCB_SCR |= SCB_SCR_SLEEPDEEP;
	PWR_CR1 &= ~PWR_CR1_LPMS_MASK;
	PWR_CR1 |= PWR_CR1_LPMS_STOP_1;
	__asm("wfi");

}


void lptim1_isr(void) {
	if (lptimer_get_flag(LPTIM1, LPTIM_ISR_CMPM)) {
		lptimer_clear_flag(LPTIM1, LPTIM_ICR_CMPMCF);
			xPortSysTickHandler();
			last_tick = lptimer_get_counter(LPTIM1);
			lptimer_set_compare(LPTIM1, last_tick + 32);
	}
	if (lptimer_get_flag(LPTIM1, LPTIM_ISR_ARRM)) {
		lptimer_clear_flag(LPTIM1, LPTIM_ICR_ARRMCF);
			lptim1_ext++;
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

