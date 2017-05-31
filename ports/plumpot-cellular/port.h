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

#pragma once

#include <stdint.h>
#include "version.h"
#include "module_led.h"
#include "module_usart.h"
#include "services/cli.h"
#include "interface_directory.h"


#define PORT_NAME                  "plumpot-cellular"
#define PORT_BANNER                "uMeshFw"

#define PORT_CLOG                  true
#define ENABLE_PROFILING           false

/* BUG: enabline clog reuse causes the logging buffer to be corrupted
 * when the messages wrap at the end. */
#define PORT_CLOG_REUSE            false
#define PORT_CLOG_BASE             0x20000000
#define PORT_CLOG_SIZE             0x800

extern struct module_led led_stat;
extern struct module_usart console;
extern struct module_loginmgr console_loginmgr;
extern ServiceCli console_cli;
extern struct module_spibus_locm3 spi1;
extern struct module_spidev_locm3 spi1_radio1;
extern struct module_spibus_locm3 spi2;
extern struct module_spidev_locm3 spi2_flash1;
extern struct module_spi_flash flash1;
extern struct module_rtc_locm3 rtc1;
extern struct sffs fs;
extern struct module_prng_simple prng;
extern InterfaceDirectory interfaces;
extern struct module_umesh umesh;


int32_t port_early_init(void);
#define PORT_EARLY_INIT_OK 0
#define PORT_EARLY_INIT_FAILED -1

int32_t port_init(void);
#define PORT_INIT_OK 0
#define PORT_INIT_FAILED -1

void port_task_timer_init(void);
uint32_t port_task_timer_get_value(void);



