/* SPDX-License-Identifier: BSD-2-Clause
 *
 * nwdaq-rtd18-fdc port-specific configuration
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include "version.h"
#include "interfaces/servicelocator.h"
#include "services/generic-power/generic-power.h"
#include "services/generic-mux/generic-mux.h"
#include "services/adc-mcp3564/mcp3564.h"
#include <services/stm32-rtc/rtc.h>
#include <services/adc-sensor/adc-sensor.h>
#include <interfaces/mux.h>


#define PORT_NAME                  "nwdaq-rtd18-fdc"
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
#define EXC_DIS_PORT GPIOA
#define EXC_DIS_PIN GPIO9

/* ADC_MCLK on TIM2, channel 3 */
#define ADC_MCLK_PORT GPIOB
#define ADC_MCLK_PIN GPIO10
#define ADC_MCLK_AF GPIO_AF1

/* ADC_IRQ on TIM2, channel 4 */
#define ADC_IRQ_PORT GPIOB
#define ADC_IRQ_PIN GPIO11
#define ADC_IRQ_AF GPIO_AF1

#define ADC_CS_PORT GPIOC
#define ADC_CS_PIN GPIO6

#define MUX_EN_PORT GPIOB
#define MUX_EN_PIN GPIO0

#define MUX_A0_PORT GPIOC
#define MUX_A0_PIN GPIO4
#define MUX_A1_PORT GPIOB
#define MUX_A1_PIN GPIO2
#define MUX_A2_PORT GPIOB
#define MUX_A2_PIN GPIO1

#define FLASH_CS_PORT GPIOC
#define FLASH_CS_PIN GPIO11

#define I2C1_SDA_PORT GPIOB
#define I2C1_SDA_PIN GPIO7
#define I2C1_SDA_AF GPIO_AF4

#define I2C1_SCL_PORT GPIOA
#define I2C1_SCL_PIN GPIO15
#define I2C1_SCL_AF GPIO_AF4


#define CAN_SHDN_PORT GPIOA
#define CAN_SHDN_PIN GPIO10
#define CAN_RX_PORT GPIOA
#define CAN_RX_PIN GPIO11
#define CAN_TX_PORT GPIOA
#define CAN_TX_PIN GPIO12
#define CAN_AF GPIO_AF9
#define CAN_BITRATE 125000

/* STM32G4 unique 96 bit identifier */
#define UNIQUE_ID_REG ((void *)0x1fff7590)
#define UNIQUE_ID_REG_LEN 12


extern IServiceLocator *locator;
extern GenericMux input_mux;
extern Mux mcp_mux;
extern Mcp3564 mcp;
extern Stm32Rtc rtc;
extern AdcSensor pcb_temp;

int32_t port_early_init(void);
#define PORT_EARLY_INIT_OK 0
#define PORT_EARLY_INIT_FAILED -1

int32_t port_init(void);
#define PORT_INIT_OK 0
#define PORT_INIT_FAILED -1

void port_task_timer_init(void);
uint32_t port_task_timer_get_value(void);


