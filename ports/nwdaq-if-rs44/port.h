/* SPDX-License-Identifier: proprietary
 *
 * lb-console-b-main port
 *
 * Copyright (c) 2024, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include "version.h"
#include "interfaces/servicelocator.h"
#include "services/generic-power/generic-power.h"
#include <services/stm32-rtc/rtc.h>
#include <services/adc-sensor/adc-sensor.h>
#include <services/stm32-swuart/stm32-swuart.h>


#define PORT_NAME                  "nwdaq-if-rs44"
#define PORT_BANNER                "plumCore"

#define PORT_CLOG                  true
#define ENABLE_PROFILING           false

/* BUG: enabline clog reuse causes the logging buffer to be corrupted
 * when the messages wrap at the end. */
#define PORT_CLOG_REUSE            false
#define PORT_CLOG_BASE             0x20000000
#define PORT_CLOG_SIZE             0x800

/**
 * GPIO definitions
 */


/* STM32G4 unique 96 bit identifier */
#define UNIQUE_ID_REG ((void *)0x1fff7590)
#define UNIQUE_ID_REG_LEN 12


extern IServiceLocator *locator;

int32_t port_early_init(void);
#define PORT_EARLY_INIT_OK 0
#define PORT_EARLY_INIT_FAILED -1

int32_t port_init(void);
#define PORT_INIT_OK 0
#define PORT_INIT_FAILED -1


