/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nwdaq-if-canlp port-specific configuration
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include "version.h"
#include "interfaces/servicelocator.h"
#include <services/stm32-rtc/rtc.h>


#define PORT_NAME                  "nwdaq-if-canlp"
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
#define CAN1_SHDN_PORT GPIOA
#define CAN1_SHDN_PIN GPIO10
#define CAN1_RX_PORT GPIOA
#define CAN1_RX_PIN GPIO11
#define CAN1_TX_PORT GPIOA
#define CAN1_TX_PIN GPIO12
#define CAN1_AF GPIO_AF9
#define CAN1_BITRATE 1000000

#define CAN2_SHDN_PORT GPIOB
#define CAN2_SHDN_PIN GPIO7
#define CAN2_RX_PORT GPIOB
#define CAN2_RX_PIN GPIO5
#define CAN2_TX_PORT GPIOB
#define CAN2_TX_PIN GPIO6
#define CAN2_AF GPIO_AF9
#define CAN2_BITRATE 125000

#define LED_WH_PORT GPIOA
#define LED_WH_PIN GPIO4
#define LED_GN_PORT GPIOA
#define LED_GN_PIN GPIO5
#define LED_RD_PORT GPIOA
#define LED_RD_PIN GPIO6

/* STM32G4 unique 96 bit identifier */
#define UNIQUE_ID_REG ((void *)0x1fff7590)
#define UNIQUE_ID_REG_LEN 12


extern IServiceLocator *locator;
extern Stm32Rtc rtc;

int32_t port_early_init(void);
#define PORT_EARLY_INIT_OK 0
#define PORT_EARLY_INIT_FAILED -1

int32_t port_init(void);
#define PORT_INIT_OK 0
#define PORT_INIT_FAILED -1

void port_task_timer_init(void);
uint32_t port_task_timer_get_value(void);


