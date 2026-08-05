/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * nwdaq-main-hh1 basic port (STM32U575VIT6)
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include "config.h"
#include "version.h"
#include "interfaces/servicelocator.h"


#define PORT_NAME                  "nwdaq-main-hh1"
#define PORT_BANNER                "plumCore"

#define PORT_CLOG                  true
#define ENABLE_PROFILING           false

/* BUG: enabling clog reuse causes the logging buffer to be corrupted
 * when the messages wrap at the end. */
#define PORT_CLOG_REUSE            false
#define PORT_CLOG_BASE             0x20000000
#define PORT_CLOG_SIZE             0x800


/* STM32U5 unique 96 bit identifier */
#define UNIQUE_ID_REG ((void *)0x0bfa0700)
#define UNIQUE_ID_REG_LEN 12


extern IServiceLocator *locator;

int32_t port_early_init(void);
#define PORT_EARLY_INIT_OK 0
#define PORT_EARLY_INIT_FAILED -1

int32_t port_init(void);
#define PORT_INIT_OK 0
#define PORT_INIT_FAILED -1
