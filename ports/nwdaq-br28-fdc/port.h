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
#include "services/generic-power/generic-power.h"
#include "services/generic-mux/generic-mux.h"
#include "services/adc-mcp3564/mcp3564.h"


#define PORT_NAME                  "nwdaq-b28-fdc"
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
#define CAN_SHDN_PORT GPIOA
#define CAN_SHDN_PIN GPIO10
#define CAN_RX_PORT GPIOA
#define CAN_RX_PIN GPIO11
#define CAN_TX_PORT GPIOA
#define CAN_TX_PIN GPIO12
#define CAN_AF GPIO_AF9

#define EXC_EN_PORT GPIOA
#define EXC_EN_PIN GPIO8

/* ADC_MCLK on TIM2, channel 3 */
#define ADC_MCLK_PORT GPIOB
#define ADC_MCLK_PIN GPIO10
#define ADC_MCLK_AF GPIO_AF1

/* ADC_IRQ on TIM2, channel 4 */
#define ADC_IRQ_PORT GPIOB
#define ADC_IRQ_PIN GPIO11
#define ADC_IRQ_AF GPIO_AF1

#define LED_STAT_PORT GPIOB
#define LED_STAT_PIN GPIO6

#define LED_ERROR_PORT GPIOC
#define LED_ERROR_PIN GPIO10

#define ADC_CS_PORT GPIOB
#define ADC_CS_PIN GPIO12

#define VDDA_SW_EN_PORT GPIOA
#define VDDA_SW_EN_PIN GPIO1

#define VREF1_DAC_PORT GPIOA
#define VREF1_DAC_PIN GPIO4
#define VREF2_DAC_PORT GPIOA
#define VREF2_DAC_PIN GPIO5

#define VREF_M (0)
#define VREF_P (4095)

#define VREF1_SEL_PORT GPIOB
#define VREF1_SEL_PIN GPIO1
#define VREF2_SEL_PORT GPIOB
#define VREF2_SEL_PIN GPIO2

#define MUX_EN_PORT GPIOB
#define MUX_EN_PIN GPIO0

#define MUX_A0_PORT GPIOC
#define MUX_A0_PIN GPIO4
#define MUX_A1_PORT GPIOA
#define MUX_A1_PIN GPIO7
#define MUX_A2_PORT GPIOA
#define MUX_A2_PIN GPIO6

#define FLASH_CS_PORT GPIOC
#define FLASH_CS_PIN GPIO11


extern IServiceLocator *locator;
extern GenericPower exc_power;
extern GenericMux input_mux;
extern Mcp3564 mcp;

int32_t port_early_init(void);
#define PORT_EARLY_INIT_OK 0
#define PORT_EARLY_INIT_FAILED -1

int32_t port_init(void);
#define PORT_INIT_OK 0
#define PORT_INIT_FAILED -1

void port_task_timer_init(void);
uint32_t port_task_timer_get_value(void);


