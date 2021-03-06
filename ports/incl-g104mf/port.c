#include <stdint.h>
#include <inttypes.h>
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
#include <libopencm3/stm32/dbgmcu.h>

#include "config.h"
#include "FreeRTOS.h"
#include "timers.h"
#include "u_assert.h"
#include "u_log.h"

#include "port.h"

/** @todo redo module and interface, keep the init here. */
#include "module_led.h"
#include "interface_led.h"

/** @todo redo module and interface, keep the init here. */
#include "module_usart.h"
#include "interface_stream.h"

/** @todo redo the whole SPI layer and flash driver */
#include "interface_spibus.h"
#include "module_spibus_locm3.h"
#include "interface_flash.h"
#include "module_spi_flash.h"
#include "interface_spidev.h"
#include "module_spidev_locm3.h"

/** @todo deprecated, remove */
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
#include "services/cli/system_cli_tree.h"
#include "services/stm32-system-clock/clock.h"
#include "services/stm32-rtc/rtc.h"
#include "services/icm42688p/icm42688p.h"
#include "interfaces/i2c-bus.h"
#include "services/stm32-i2c/stm32-i2c.h"
#include "services/bq35100/bq35100.h"
#include "services/spi-sd/spi-sd.h"
#include "services/gps-ublox/gps-ublox.h"


/**
 * Port specific global variables and singleton instances.
 */

/* Old-style HAL */
uint32_t SystemCoreClock;
struct module_rtc_locm3 rtc1;
struct module_prng_simple prng;
struct module_fifo_profiler profiler;
struct module_spibus_locm3 spi1;
struct module_spidev_locm3 spi1_accel2;

#define USART_CR3_UCESM (1 << 23)
#define USART_CR3_WUS_START (2 << 20)


#if defined(CONFIG_SERVICE_STM32_WATCHDOG)
	#include "services/stm32-watchdog/watchdog.h"
	Watchdog watchdog;
#endif

SystemClock system_clock;
Stm32Rtc rtc;
Icm42688p accel2;
Stm32I2c i2c1;
Bq35100 bq35100;
GpsUblox gps;

#define RCC_CCIPR_LPTIM1SEL_LSE (3 << 18)
volatile uint16_t last_tick;
volatile uint16_t lptim1_ext;

void vPortSetupTimerInterrupt(void);
void port_sleep(TickType_t idle_time);

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
	RCC_APB1SMENR1 = RCC_APB1SMENR1_LPTIM1SMEN;
	RCC_APB1SMENR2 = 0;
	RCC_APB2SMENR = RCC_APB2SMENR_USART1SMEN;

	return PORT_EARLY_INIT_OK;
}


static int64_t timespec_diff(struct timespec *time1, struct timespec *time2) {
	return (time1->tv_sec * 1000000000UL + time1->tv_nsec)
	     - (time2->tv_sec * 1000000000UL + time2->tv_nsec);
}


struct module_usart console;
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
	RCC_CCIPR |= RCC_CCIPR_USART1SEL_HSI16;

	nvic_enable_irq(NVIC_USART1_IRQ);
	nvic_set_priority(NVIC_USART1_IRQ, 6 * 16);
	module_usart_init(&console, "console", USART1);
	module_usart_set_baudrate(&console, 115200);
	hal_interface_set_name(&(console.iface.descriptor), "console");

	/* Console is now initialized, set its stream to be used as u_log output. */
	u_log_set_stream(&(console.iface));

	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_STREAM, (Interface *)&console.iface.descriptor, "console");
}


struct module_led led_status;
struct module_led led_error;
static void led_init(void) {
	/* Allow reprogramming before disabling SWD. */
	u_log(system_log, LOG_TYPE_INFO, "disabling SWD access");
	vTaskDelay(5000);
	
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO14);
	module_led_init(&led_status, "led_status");
	module_led_set_port(&led_status, GPIOA, GPIO14);
	interface_led_loop(&led_status.iface, 0x11f1);
	hal_interface_set_name(&(led_status.iface.descriptor), "led_status");
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_LED, (Interface *)&led_status.iface.descriptor, "led_status");

	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO13);
	module_led_init(&led_error, "led_error");
	module_led_set_port(&led_error, GPIOA, GPIO13);
	// interface_led_loop(&led_error.iface, 0x11f1);
	hal_interface_set_name(&(led_error.iface.descriptor), "led_error");
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_LED, (Interface *)&led_error.iface.descriptor, "led_error");
}


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


void port_accel2_init(void) {
	/* ICM42688P on SPI1 bus */
	gpio_set(GPIOB, GPIO8);
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO8);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO8);
	module_spidev_locm3_init(&spi1_accel2, "spi1_accel2", &(spi1.iface), GPIOB, GPIO8);
	hal_interface_set_name(&(spi1_accel2.iface.descriptor), "spi1_accel2");

	/* SYNC32 output */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO2);

	icm42688p_init(&accel2, &spi1_accel2.iface);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_WAVEFORM_SOURCE, (Interface *)&accel2.source, "accel2");
}



/*****************************************************************************************************
 * u-blox MAX-M8Q initialization & ISR
 *****************************************************************************************************/

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


	struct timespec last_measure_time = {0};
	struct timespec last_timepulse_time = {0};

	while (true) {
		port_gps_power(true);
		gps_ublox_start(&gps);
		exti_enable_request(EXTI8);
		uint32_t i = 100;
		while (true) {
			vTaskDelay(1000);

			/* Read all */
			struct timespec measure_time = {0};
			struct timespec timepulse_time = {0};
			int32_t timepulse_accuracy = 0;
			uint32_t timepulse_count = 0;
			UBaseType_t cs = taskENTER_CRITICAL_FROM_ISR();
			measure_time = gps.measure_time;
			timepulse_time = gps.timepulse_time;
			timepulse_accuracy = gps.timepulse_accuracy;
			timepulse_count = gps.timepulse_count;
			taskEXIT_CRITICAL_FROM_ISR(cs);
		
			
			// u_log(system_log, LOG_TYPE_DEBUG, "timepulse accuracy %d ns", timepulse_accuracy);
			// u_log(system_log, LOG_TYPE_DEBUG, "bat U = %u mV, I = %d mA", bq35100_voltage(&bq35100), bq35100_current(&bq35100));
			i--;
			if (!i) {
				u_log(system_log, LOG_TYPE_ERROR, "cannot achieve target timepulse accuracy < 500 ns (current %lu ns), timeout", timepulse_accuracy);
				break;
			}
			if (timepulse_accuracy >= 0 && timepulse_accuracy < 500 && timepulse_count > 1) {
				u_log(system_log, LOG_TYPE_INFO, "achieved timepulse accuracy < 500 ns (%lu ns), count %lu", timepulse_accuracy, timepulse_count);
				
				/* todo */
				char buf[30] = {0};

				struct tm tm = {0};
				gmtime_r(&timepulse_time.tv_sec, &tm);
				strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.", &tm);
				u_log(system_log, LOG_TYPE_INFO, "timepulse time %s%09luZ", buf, timepulse_time.tv_nsec);
				
				gmtime_r(&measure_time.tv_sec, &tm);
				strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.", &tm);
				u_log(system_log, LOG_TYPE_INFO, "measured RTC time %s%09luZ", buf, measure_time.tv_nsec);

				int32_t diff_us = timespec_diff(&timepulse_time, &measure_time) / 1000LL;
				u_log(system_log, LOG_TYPE_DEBUG, "time difference %ld us", diff_us);

				#if 1
				if (abs(diff_us) > 2000000L) {
					u_log(system_log, LOG_TYPE_INFO, "step time change");
					rtc.clock.set(rtc.clock.parent, &timepulse_time);
					/* Do not break, do a shift to set the clock precisely. */
					// break;
				} else if (abs(diff_us) > 1000L) {
					if (rtc.clock.shift) {
						rtc.clock.shift(rtc.clock.parent, diff_us * 1000L);
					}
					last_measure_time = (struct timespec){0};
					last_timepulse_time = (struct timespec){0};
				} else {

					/* Correct the speed. */
					if (last_measure_time.tv_sec) {
						int64_t diff_measure_time_ns = timespec_diff(&measure_time, &last_measure_time);
						int64_t diff_timepulse_time_ns = timespec_diff(&timepulse_time, &last_timepulse_time);
						int64_t correct_ppb = (diff_timepulse_time_ns - diff_measure_time_ns) * 1000LL / (diff_timepulse_time_ns / 1000000LL);
						u_log(system_log, LOG_TYPE_INFO, "diff_measured = %ld ns, diff_timepulse = %ld ns, correct_by = %ld ppb", (int32_t)diff_measure_time_ns, (int32_t)diff_timepulse_time_ns, (int32_t)correct_ppb);

						last_measure_time = measure_time;
						last_timepulse_time = timepulse_time;
						break;
					}
					last_measure_time = measure_time;
					last_timepulse_time = timepulse_time;
				}


				#endif
			}
		}
		exti_disable_request(EXTI8);
		gps_ublox_stop(&gps);
		port_gps_power(false);
		vTaskDelay(600000);

		// if (gps.lptim_arr_adjust != 0) {
			// u_log(system_log, LOG_TYPE_DEBUG, "lptim_adjust = %d, lptim_arr_adjust = %d, accumulator = %d", gps.lptim_adjust, gps.lptim_arr_adjust, gps.lptim_accumulator);
		// }
		// u_log(system_log, LOG_TYPE_DEBUG, "cas %lu", gps.last_ns);
		// vTaskDelay(1000);
	}
}


void port_gps_power(bool en) {
	if (en) {
		gpio_set(GPIOH, GPIO1);
		vTaskDelay(100);
	} else {
		gpio_clear(GPIOH, GPIO1);
	}
}


/*****************************************************************************************************
 * micro-SD card initialization and support functions
 *****************************************************************************************************/

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


void port_battery_gauge_init(void) {
	rcc_periph_clock_enable(RCC_I2C1);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6 | GPIO9);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, GPIO6 | GPIO9);
	gpio_set_af(GPIOB, GPIO_AF4, GPIO6 | GPIO9);
	stm32_i2c_init(&i2c1, I2C1);

	/* BQ35100 test */
	bq35100_init(&bq35100, &i2c1.bus);
	while (false) {
		u_log(system_log, LOG_TYPE_DEBUG, "bat U = %u mV, I = %d mA", bq35100_voltage(&bq35100), bq35100_current(&bq35100));
		vTaskDelay(1000);
	}
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

	/* USB_VBUS_DET */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO4);

	/* LORA_PWR_EN */
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO0);
	gpio_clear(GPIOA, GPIO0);

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

	console_init();

	/** @deprecated
	 * Old real-time clock service will be removed. There is
	 * a new clock interface and services providing system and RTC clocks. */
	/* Initialize the Real-time clock. */
	module_rtc_locm3_init(&rtc1, "rtc1");
	hal_interface_set_name(&(rtc1.iface.descriptor), "rtc1");
	u_log_set_rtc(&(rtc1.iface));
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_RTC, (Interface *)&rtc1.iface.descriptor, "rtc1");

	#if defined(CONFIG_INCL_G104MF_ENABLE_LEDS)
		led_init();
	#endif

	#if defined(CONFIG_INCL_G104MF_ENABLE_WATCHDOG)
		watchdog_init(&watchdog, 20000, 0);
	#endif

	rcc_periph_clock_enable(RCC_TIM2);
	rcc_periph_reset_pulse(RST_TIM2);
	nvic_enable_irq(NVIC_TIM2_IRQ);

	/* 1 MHz main monotonic clock */
	system_clock_init(&system_clock, TIM2, SystemCoreClock / 1e6 - 1, UINT32_MAX);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CLOCK, &system_clock.iface.interface, "main");

	/* New-style RTC driver and interface */
	stm32_rtc_init(&rtc);
	iservicelocator_add(locator, ISERVICELOCATOR_TYPE_CLOCK, &rtc.iface.interface, "rtc");

	port_battery_gauge_init();
	port_gps_power(false);
	// port_gps_init();
	port_sd_init();

	/* SPI1 with three accelerometers and a radio */
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO3 | GPIO4 | GPIO5);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO3 | GPIO4 | GPIO5);
	gpio_set_af(GPIOB, GPIO_AF5, GPIO3 | GPIO4 | GPIO5);
	rcc_periph_clock_enable(RCC_SPI1);
	module_spibus_locm3_init(&spi1, "spi1", SPI1);
	hal_interface_set_name(&(spi1.iface.descriptor), "spi1");

	port_rfm_init();
	port_accel2_init();

	return PORT_INIT_OK;
}


void tim2_isr(void) {
	if (TIM_SR(TIM2) & TIM_SR_UIF) {
		timer_clear_flag(TIM2, TIM_SR_UIF);
		system_clock_overflow_handler(&system_clock);
	}
}


/*****************************************************************************************************
 * Low power timer (LPTIM) for FreeRTOS timing
 *****************************************************************************************************/

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


void usart1_isr(void) {
	module_usart_interrupt_handler(&console);
}


void vApplicationIdleHook(void);
void vApplicationIdleHook(void) {
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


void port_sleep(TickType_t idle_time) {
	eSleepModeStatus sleep_status;

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


