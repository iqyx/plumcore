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
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/exti.h>

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


int32_t port_early_init(void) {
	/* Relocate the vector table first if required. */
	#if defined(CONFIG_RELOCATE_VECTOR_TABLE)
		SCB_VTOR = CONFIG_VECTOR_TABLE_ADDRESS;
	#endif

	#if defined(CONFIG_INCL_G104MF_CLOCK_80MHZ)
		/* placeholder */
	#elif defined(CONFIG_INCL_G104MF_CLOCK_DEFAULT)
		/* Do not intiialize anything, keep MSI clock running. */
		SystemCoreClock = 4e6;
	#else
		#error "no clock speed defined"
	#endif

	rcc_apb1_frequency = SystemCoreClock;
	rcc_apb2_frequency = SystemCoreClock;

	/* Initialize systick interrupt for FreeRTOS. */
	nvic_set_priority(NVIC_SYSTICK_IRQ, 255);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(SystemCoreClock / 1000 - 1);
	systick_interrupt_enable();
	systick_counter_enable();

	/* Initialize all required clocks for GPIO ports. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* Timer 11 needs to be initialized prior to starting the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM16);

	return PORT_EARLY_INIT_OK;
}


int32_t port_init(void) {

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

	/* Analog inputs (temperature measurement) */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO5 | GPIO6);

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


/* Configure dedicated timer (tim3) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {
	rcc_periph_reset_pulse(RST_TIM16);
	/* The timer should run at 10kHz */
	timer_set_prescaler(TIM16, SystemCoreClock / 10000 - 1);
	timer_continuous_mode(TIM16);
	timer_set_period(TIM16, UINT16_MAX);
	timer_enable_counter(TIM16);
}


uint32_t port_task_timer_get_value(void) {
	return timer_get_counter(TIM16);
}


void usart1_isr(void) {
	module_usart_interrupt_handler(&console);
}

