/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nwdaq-br28-fdc port-specific configuration
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include "version.h"
#include "interfaces/servicelocator.h"


#define PORT_NAME                  "nwdaq-b28-fdc"
#define PORT_BANNER                "plumCore"

#define PORT_CLOG                  true
#define ENABLE_PROFILING           false

/* BUG: enabline clog reuse causes the logging buffer to be corrupted
 * when the messages wrap at the end. */
#define PORT_CLOG_REUSE            false
#define PORT_CLOG_BASE             0x20000000
#define PORT_CLOG_SIZE             0x800

extern IServiceLocator *locator;

int32_t port_early_init(void);
#define PORT_EARLY_INIT_OK 0
#define PORT_EARLY_INIT_FAILED -1

int32_t port_init(void);
#define PORT_INIT_OK 0
#define PORT_INIT_FAILED -1

void port_task_timer_init(void);
uint32_t port_task_timer_get_value(void);

int32_t system_test(void);
void port_enable_swd(bool e);


