/* SPDX-License-Identifier: BSD-2-Clause
 *
 * plumCore entry point and main system include file
 *
 * Copyright (c) 2015-2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

/* Configuration generated from the Kconfig's .config. Must be the first. */
#include "config.h"

#include "platform.h"
#include "port.h"
#include "system.h"

/* Scheduler and IPC includes */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "stream_buffer.h"

/* Global helper routines */
#include "u_assert.h"
#include "u_log.h"


