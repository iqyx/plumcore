/*
 * plumpot-cellular port-specific configuration
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/exti.h>

#include "FreeRTOS.h"
#include "timers.h"
#include "u_assert.h"
#include "u_log.h"
#include "port.h"

/* Old one. */
#include "hal_interface_directory.h"
/* New one. */
#include "interface_directory.h"

#include "module_led.h"
#include "interface_led.h"
#include "module_usart.h"
#include "interface_stream.h"
#include "module_loginmgr.h"
#include "services/cli.h"
#include "services/cli/system_cli_tree.h"
#include "interface_spibus.h"
#include "module_spibus_locm3.h"
#include "interface_flash.h"
#include "module_spi_flash.h"
#include "interface_spidev.h"
#include "module_spidev_locm3.h"
#include "interface_rtc.h"
#include "module_rtc_locm3.h"
#include "sffs.h"
#include "interface_rng.h"
#include "module_prng_simple.h"
#include "uhal/interfaces/adc.h"
#include "uhal/modules/adc_stm32_locm3.h"
#include "module_power_adc.h"
#include "module_mac_csma.h"
#include "interface_mac.h"
#include "module_umesh.h"
#include "interface_profiling.h"
#include "module_fifo_profiler.h"

#include "uhal/interfaces/sensor.h"
#include "uhal/modules/stm32_timer_capsense.h"
#include "uhal/modules/ax5243.h"
#include "uhal/modules/gsm_quectel.h"
#include "uhal/modules/i2c_sensors.h"

#include "umesh_l2_status.h"
#include "services/sensor_upload.h"

#include "uhal/interfaces/cellular.h"
#include "uxb_locm3.h"
#include "uhal/modules/solar_charger.h"

#include "protocols/uxb/solar_charger_iface.pb.h"
#include "pb_decode.h"

#include "services/watchdog.h"


/**
 * Port specific global variables and singleton instances.
 */
uint32_t SystemCoreClock;
struct module_led led_charge;
struct module_led led_stat;
struct module_led led_tx;
struct module_led led_rx;
struct module_usart console;
struct module_loginmgr console_loginmgr;
ServiceCli console_cli;
struct module_spibus_locm3 spi2;
struct module_spidev_locm3 spi2_flash1;
struct module_spidev_locm3 spi2_radio1;
struct module_spi_flash flash1;
struct module_rtc_locm3 rtc1;
struct module_prng_simple prng;
AdcStm32Locm3 adc1;
InterfaceDirectory interfaces;
Ax5243 radio1;
struct module_mac_csma radio1_mac;
struct module_umesh umesh;
struct module_fifo_profiler profiler;
GsmQuectel gsm1;
struct module_usart gsm1_usart;
SensorUpload upload1;
I2cSensors i2c_test;
struct sffs fs;
UxbMasterLocm3 uxb;
UxbInterface uxb_iface1;
UxbSlot uxb_slot1;
uint8_t slot1_buffer[256];
uxb_master_locm3_ret_t uxb_ret;
SolarCharger solar_charger1;
Watchdog watchdog;


struct module_power_adc vin1;
struct module_power_adc_config vin1_config = {
	.measure_voltage = true,
	.voltage_channel = 5,
	.voltage_multiplier = 488,
	.voltage_divider = 18
};

struct module_power_adc ubx_voltage;
struct module_power_adc_config ubx_voltage_config = {
	.measure_voltage = true,
	.voltage_channel = 6,
	.voltage_multiplier = 2,
	.voltage_divider = 1
};


struct hal_interface_descriptor *hal_interfaces[] = {
	&ubx_voltage.iface.descriptor,
	&vin1.iface.descriptor,
	NULL
};


static struct interface_directory_item interface_list[] = {
	{
		.type = INTERFACE_TYPE_SENSOR,
		.name = "meteo_temp",
		.interface = &i2c_test.si7021_temp.interface,
	}, {
		.type = INTERFACE_TYPE_SENSOR,
		.name = "meteo_rh",
		.interface = &i2c_test.si7021_rh.interface,
	}, {
		.type = INTERFACE_TYPE_SENSOR,
		.name = "meteo_lum",
		.interface = &i2c_test.lum.interface,
	}, {
		.type = INTERFACE_TYPE_CELLULAR,
		.name = "gsm1",
		.interface = &gsm1.cellular.interface,
	}, {
		.type = INTERFACE_TYPE_SENSOR,
		.name = "charger_board_temp",
		.interface = &solar_charger1.board_temperature.interface,
	}, {
		.type = INTERFACE_TYPE_SENSOR,
		.name = "charger_bat_voltage",
		.interface = &solar_charger1.battery_voltage.interface,
	}, {
		.type = INTERFACE_TYPE_SENSOR,
		.name = "charger_bat_current",
		.interface = &solar_charger1.battery_current.interface,
	}, {
		.type = INTERFACE_TYPE_SENSOR,
		.name = "charger_bat_charge",
		.interface = &solar_charger1.battery_charge.interface,
	}, {
		.type = INTERFACE_TYPE_SENSOR,
		.name = "charger_bat_temp",
		.interface = &solar_charger1.battery_temperature.interface,
	}, {
		.type = INTERFACE_TYPE_NONE
	}
};


int32_t port_early_init(void) {
	/* Relocate the vector table first. */
	SCB_VTOR = 0x00010400;

	rcc_set_hpre(RCC_CFGR_HPRE_DIV_NONE);
	rcc_set_ppre1(RCC_CFGR_PPRE_DIV_NONE);
	rcc_set_ppre2(RCC_CFGR_PPRE_DIV_NONE);

	/* Initialize systick interrupt for FreeRTOS. */
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(15999);
	systick_interrupt_enable();
	systick_counter_enable();

	/* System clock is now at 16MHz (without PLL). */
	SystemCoreClock = 16000000;
	rcc_apb1_frequency = 16000000;
	rcc_apb2_frequency = 16000000;

	/* Initialize all required clocks for GPIO ports. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_GPIOC);

	/* USART1 is used as a system console. */
	rcc_periph_clock_enable(RCC_USART1);
	nvic_enable_irq(NVIC_USART1_IRQ);
	nvic_set_priority(NVIC_USART1_IRQ, 6 * 16);

	/* Timer 11 needs to be initialized prior to startint the scheduler. It is
	 * used as a reference clock for getting task statistics. */
	rcc_periph_clock_enable(RCC_TIM11);

	return PORT_EARLY_INIT_OK;
}


static void port_radio1_timer_callback(TimerHandle_t timer) {
}


int32_t port_init(void) {

	/* Status LEDs. */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO10);

	/* Serial console. */
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO6);
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO7);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO6);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO7);
	gpio_set_af(GPIOB, GPIO_AF7, GPIO6);
	gpio_set_af(GPIOB, GPIO_AF7, GPIO7);


	/* Radio module (SPI). */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO2);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO2);
	gpio_set(GPIOB, GPIO2);


	/* Analog inputs (battery measurement). */
	gpio_mode_setup(GPIOA, GPIO_MODE_ANALOG, GPIO_PUPD_NONE, GPIO5 | GPIO6);


	/* Main system console is created on the first USART interface. USART1 clock
	 * is already enabled. */
	module_usart_init(&console, "console", USART1);
	hal_interface_set_name(&(console.iface.descriptor), "console");

	/* Console is now initialized, set its stream to be used as u_log
	 * output. */
	u_log_set_stream(&(console.iface));

	/* Initialize the Real-time clock. */
	module_rtc_locm3_init(&rtc1, "rtc1");
	hal_interface_set_name(&(rtc1.iface.descriptor), "rtc1");
	u_log_set_rtc(&(rtc1.iface));

	module_led_init(&led_stat, "led_stat");
	module_led_set_port(&led_stat, GPIOB, GPIO10);
	interface_led_loop(&(led_stat.iface), 0x0);
	hal_interface_set_name(&(led_stat.iface.descriptor), "led_stat");

	module_prng_simple_init(&prng, "prng");
	hal_interface_set_name(&(prng.iface.descriptor), "prng");

	module_fifo_profiler_init(&profiler, "profiler", TIM3, 100);
	hal_interface_set_name(&(profiler.iface.descriptor), "profiler");
	module_fifo_profiler_stream_enable(&profiler, &(console.iface));


	/* Start login manager in the main system console. */
	module_loginmgr_init(&console_loginmgr, "login1", &(console.iface));
	hal_interface_set_name(&(console_loginmgr.iface.descriptor), "login1");

	service_cli_init(&console_cli, &(console_loginmgr.iface), system_cli_tree);
	service_cli_start(&console_cli);

	/* Configure GPIO for radio & flash SPI bus (SPI2), enable SPI2 clock and
	 * run spibus driver using libopencm3 to access it. */
	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO13 | GPIO14 | GPIO15);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO13 | GPIO14 | GPIO15);
	gpio_set_af(GPIOB, GPIO_AF5, GPIO13 | GPIO14 | GPIO15);

	rcc_periph_clock_enable(RCC_SPI2);

	module_spibus_locm3_init(&spi2, "spi2", SPI2);
	hal_interface_set_name(&(spi2.iface.descriptor), "spi2");

	/* Initialize SPI device on the SPI2 bus. */
	gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO12);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO12);

	module_spidev_locm3_init(&spi2_flash1, "spi2_flash1", &(spi2.iface), GPIOB, GPIO12);
	hal_interface_set_name(&(spi2_flash1.iface.descriptor), "spi2_flash1");

	/* Initialize flash device on the SPI2 bus. */
	module_spi_flash_init(&flash1, "flash1", &(spi2_flash1.iface));
	hal_interface_set_name(&(flash1.iface.descriptor), "flash1");

	/* TODO: move to a dedicated module. */
	sffs_init(&fs);
	if (sffs_mount(&fs, &(flash1.iface.descriptor)) == SFFS_MOUNT_OK) {
		u_log(system_log, LOG_TYPE_INFO, "sffs: filesystem mounted successfully");

		struct sffs_info info;
		if (sffs_get_info(&fs, &info) == SFFS_GET_INFO_OK) {
			u_log(system_log, LOG_TYPE_INFO,
				"sffs: sectors t=%u e=%u u=%u f=%u d=%u o=%u, pages t=%u e=%u u=%u o=%u",
				info.sectors_total,
				info.sectors_erased,
				info.sectors_used,
				info.sectors_full,
				info.sectors_dirty,
				info.sectors_old,

				info.pages_total,
				info.pages_erased,
				info.pages_used,
				info.pages_old
			);
			u_log(system_log, LOG_TYPE_INFO,
				"sffs: space total %u bytes, used %u bytes, free %u bytes",
				info.space_total,
				info.space_used,
				info.space_total - info.space_used
			);
		}
	}

	/* ADC initialization (power metering, prng seeding) */
	adc_stm32_locm3_init(&adc1, ADC1);

	/* Battery power device (using ADC). */
	module_power_adc_init(&vin1, "vin1", &(adc1.iface), &vin1_config);
	module_power_adc_init(&ubx_voltage, "ubx1", &(adc1.iface), &ubx_voltage_config);

	// gpio_mode_setup(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO2);
	// gpio_set_output_options(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO2);
	// module_spidev_locm3_init(&spi2_radio1, "spi2_radio1", &(spi2.iface), GPIOB, GPIO2);
	// hal_interface_set_name(&(spi2_radio1.iface.descriptor), "spi2_radio1");

	// ax5243_init(&radio1, &(spi2_radio1.iface), 16000000);
	// hal_interface_set_name(&(radio1.iface.descriptor), "radio1");


	/* Start CSMA MAC in the radio1 interface. */
	// module_mac_csma_init(&radio1_mac, "radio1_mac", &(radio1.iface));
	// hal_interface_set_name(&(radio1_mac.iface.descriptor), "radio1_mac");
	// interface_mac_start(&(radio1_mac.iface));

	/* Init the whole uMesh protocol stack (it is a single module). */
	// module_umesh_init(&umesh, "umesh");
	// module_umesh_add_mac(&umesh, &(radio1_mac.iface));
	// module_umesh_set_rng(&umesh, &(prng.iface));
	// module_umesh_set_profiler(&umesh, &(profiler.iface));

	umesh_l2_status_add_power_device(&(ubx_voltage.iface), "plumpot1_ubx");

	gsm_quectel_init(&gsm1);
	gpio_mode_setup(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO9);
	gsm_quectel_set_pwrkey(&gsm1, GPIOA, GPIO9);
	gpio_mode_setup(GPIOC, GPIO_MODE_INPUT, GPIO_PUPD_NONE, GPIO13);
	gsm_quectel_set_vddext(&gsm1, GPIOC, GPIO13);

	gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO2 | GPIO3);
	gpio_set_output_options(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO2 | GPIO3);
	gpio_set_af(GPIOA, GPIO_AF7, GPIO2 | GPIO3);
	rcc_periph_clock_enable(RCC_USART2);
	nvic_enable_irq(NVIC_USART2_IRQ);
	nvic_set_priority(NVIC_USART2_IRQ, 6 * 16);

	module_usart_init(&gsm1_usart, "gsm1_usart", USART2);
	hal_interface_set_name(&(gsm1_usart.iface.descriptor), "gsm1_usart");
	gsm_quectel_set_usart(&gsm1, &(gsm1_usart.iface));

	gsm_quectel_start(&gsm1);

	/* I2C test */

	gpio_mode_setup(GPIOB, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO8 | GPIO9);
	gpio_set_output_options(GPIOB, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, GPIO8 | GPIO9);
	gpio_set_af(GPIOB, GPIO_AF4, GPIO8 | GPIO9);

	rcc_periph_clock_enable(RCC_I2C1);
	i2c_peripheral_disable(I2C1);
	i2c_reset(I2C1);
	i2c_set_fast_mode(I2C1);
	i2c_set_clock_frequency(I2C1, I2C_CR2_FREQ_20MHZ);
	i2c_set_ccr(I2C1, 35);
	i2c_set_trise(I2C1, 43);
	i2c_peripheral_enable(I2C1);

	i2c_sensors_init(&i2c_test, I2C1);

	sensor_upload_init(&upload1, gsm_quectel_tcpip(&gsm1), "147.175.187.202", 6008);
	sensor_upload_add_power_device(&upload1, "plumpot1_uxb", &(ubx_voltage.iface), 60000);
	sensor_upload_add_power_device(&upload1, "plumpot1_vin", &(vin1.iface), 60000);
	sensor_upload_add_sensor(&upload1, "plumpot1_temp", &(i2c_test.si7021_temp), 60000);
	sensor_upload_add_sensor(&upload1, "plumpot1_rh", &(i2c_test.si7021_rh), 60000);
	sensor_upload_add_sensor(&upload1, "plumpot1_lum", &(i2c_test.lum), 60000);

	sensor_upload_add_sensor(&upload1, "charger_board_temp", &(solar_charger1.board_temperature), 60000);
	sensor_upload_add_sensor(&upload1, "charger_bat_voltage", &(solar_charger1.battery_voltage), 60000);
	sensor_upload_add_sensor(&upload1, "charger_bat_current", &(solar_charger1.battery_current), 60000);
	sensor_upload_add_sensor(&upload1, "charger_bat_charge", &(solar_charger1.battery_charge), 60000);

	interface_directory_init(&interfaces, interface_list);

	/* Initialize the UXB bus. */
	rcc_periph_clock_enable(RCC_SPI1);
	rcc_periph_clock_enable(RCC_TIM9);

	/* Setup a timer for precise UXB protocol delays. */
	timer_reset(TIM9);
	timer_set_mode(TIM9, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_continuous_mode(TIM9);
	timer_direction_up(TIM9);
	timer_disable_preload(TIM9);
	timer_enable_update_event(TIM9);
	timer_set_prescaler(TIM9, (rcc_ahb_frequency / 1000000) - 1);
	timer_set_period(TIM9, 65535);
	timer_enable_counter(TIM9);

	uxb_master_locm3_init(&uxb, &(struct uxb_master_locm3_config) {
		.spi_port = SPI1,
		.spi_af = GPIO_AF5,
		.sck_port = GPIOB, .sck_pin = GPIO3,
		.miso_port = GPIOB, .miso_pin = GPIO4,
		.mosi_port = GPIOB, .mosi_pin = GPIO5,
		.frame_port = GPIOA, .frame_pin = GPIO15,
		.delay_timer = TIM9,
		.delay_timer_freq_mhz = 1,
		.control_prescaler = SPI_CR1_BR_FPCLK_DIV_16,
		.data_prescaler = SPI_CR1_BR_FPCLK_DIV_16,
	});

	nvic_enable_irq(NVIC_EXTI15_10_IRQ);
	rcc_periph_clock_enable(RCC_SYSCFG);
	exti_select_source(EXTI15, GPIOA);
	exti_set_trigger(EXTI15, EXTI_TRIGGER_FALLING);
	exti_enable_request(EXTI15);

	uxb_interface_init(&uxb_iface1);
	uxb_interface_set_address(
		&uxb_iface1,
		(uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
		(uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22}
	);
	uxb_master_locm3_add_interface(&uxb, &uxb_iface1);

	uxb_slot_init(&uxb_slot1);
	uxb_slot_set_slot_number(&uxb_slot1, 0);
	uxb_slot_set_slot_buffer(&uxb_slot1, slot1_buffer, sizeof(slot1_buffer));
	uxb_interface_add_slot(&uxb_iface1, &uxb_slot1);

	solar_charger_init(&solar_charger1, &uxb_iface1);

	watchdog_init(&watchdog, 5000, 0);

	vTaskDelay(10);

	u_log(system_log, LOG_TYPE_DEBUG, "device descriptor:");
	for (uint8_t i = 1; i < 20; i++) {
		uxb_slot_send_data(&uxb_slot1, &i, 1, false);
		vTaskDelay(20);
		if (uxb_slot1.len == 1 && uxb_slot1.buffer[0] == 0) {
			break;
		}
		/* Zero terminate at the maximum key-value pair length. */
		uxb_slot1.buffer[63] = '\0';
		u_log(system_log, LOG_TYPE_DEBUG, "    %s", uxb_slot1.buffer);
	}

	return PORT_INIT_OK;
}


/* Configure dedicated timer (tim3) for runtime task statistics. It should be later
 * redone to use one of the system monotonic clocks with interface_clock. */
void port_task_timer_init(void) {

	timer_reset(TIM11);
	/* The timer should run at 1MHz */
	timer_set_prescaler(TIM11, 1599);
	timer_continuous_mode(TIM11);
	timer_set_period(TIM11, UINT16_MAX);
	timer_enable_counter(TIM11);

}


uint32_t port_task_timer_get_value(void) {
	return timer_get_counter(TIM11);
}

void usart1_isr(void) {
	module_usart_interrupt_handler(&console);
}

void usart2_isr(void) {
	module_usart_interrupt_handler(&gsm1_usart);
}

void exti15_10_isr(void) {
	exti_reset_request(EXTI15);

	exti_disable_request(EXTI15);
	uxb_ret = uxb_master_locm3_frame_irq(&uxb);
	exti_enable_request(EXTI15);
}
