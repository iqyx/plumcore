/* SPDX-License-Identifier: BSD-2-Clause
 *
 * incl-g104mf accelerometer/inclinometer port specific configuration
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "version.h"
#include "config.h"

#include <interfaces/sensor.h>
#include <interfaces/clock.h>

#define PORT_NAME                  "incl-g104mf"
#define PORT_BANNER                "plumCore"

#define PORT_CLOG                  true
#define ENABLE_PROFILING           false

/* BUG: enabling clog reuse causes the logging buffer to be corrupted
 * when the messages wrap at the end. */
#define PORT_CLOG_REUSE            false
#define PORT_CLOG_BASE             0x20000000
#define PORT_CLOG_SIZE             0x800

int32_t port_early_init(void);
#define PORT_EARLY_INIT_OK 0
#define PORT_EARLY_INIT_FAILED -1

int32_t port_init(void);
#define PORT_INIT_OK 0
#define PORT_INIT_FAILED -1


void port_task_timer_init(void);
uint32_t port_task_timer_get_value(void);
uint32_t lptim_get_extended(void);

void port_rfm_init(void);
void port_accel2_init(void);
void port_gps_init(void);
void port_battery_gauge_init(void);
void port_sd_init(void);

/* The port code exports some public functions to enable/disable
 * power supply to disconnectable peripherals. */
void port_sd_power(bool power);
void port_gps_power(bool en);
void port_lora_power(bool en);
void port_enable_swd(bool e);

/* The service locator has to be always available to all. */
/** @deprecated */
#include <interface_directory.h>
#include <interfaces/servicelocator.h>
extern IServiceLocator *locator;

/* Export the battery gauge directly as there is no suitable interface yet. */
/** @todo */
#include <services/bq35100/bq35100.h>
extern Bq35100 bq35100;

extern Sensor *si7006_sensor;
extern Clock *rtc_clock;

/* Old legacy modules exported directly. */
extern struct module_led led_stat;
extern struct module_spibus_locm3 spi2;
extern struct module_spidev_locm3 spi2_flash1;
extern struct module_spi_flash flash1;
extern struct module_rtc_locm3 rtc1;
extern struct module_prng_simple prng;
