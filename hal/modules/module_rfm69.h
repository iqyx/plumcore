/**
 * HopeRF RFM69CW/W/HW module driver
 *
 * Copyright (C) 2014, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/embedded/umesh)
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

#ifndef _MODULE_RFM69_H_
#define _MODULE_RFM69_H_

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "hal_module.h"
#include "interface_netdev.h"
#include "interface_spidev.h"
#include "interface_led.h"

enum module_rfm69_rstatus {
	RFM69_RSTATUS_MODEREADY =         0x8000,
	RFM69_RSTATUS_RXREADY =           0x4000,
	RFM69_RSTATUS_TXREADY =           0x2000,
	RFM69_RSTATUS_PLLLOCK =           0x1000,
	RFM69_RSTATUS_RSSI =              0x0800,
	RFM69_RSTATUS_TIMEOUT =           0x0400,
	RFM69_RSTATUS_AUTOMODE =          0x0200,
	RFM69_RSTATUS_SYNCADDRESSMATCH =  0x0100,
	RFM69_RSTATUS_FIFOFULL =          0x0080,
	RFM69_RSTATUS_FIFONOTEMPTY =      0x0040,
	RFM69_RSTATUS_FIFOLEVEL =         0x0020,
	RFM69_RSTATUS_FIFOOVERRUN =       0x0010,
	RFM69_RSTATUS_PACKETSENT =        0x0008,
	RFM69_RSTATUS_PAYLOADREADY =      0x0004,
	RFM69_RSTATUS_CRCOK =             0x0002,
};

enum module_rfm69_rmode {
	RFM69_RMODE_SLEEP = 0,
	RFM69_RMODE_STDBY = 1,
	RFM69_RMODE_FS =    2,
	RFM69_RMODE_TX =    3,
	RFM69_RMODE_RX =    4,
};

enum module_rfm69_state {
	RFM69_STATE_UNINITIALIZED = 0,
	RFM69_STATE_STOPPED,
	RFM69_STATE_IDLE,
	RFM69_STATE_RECEIVING,
	RFM69_STATE_TRANSMITTING
};

enum module_rfm69_led_type {
	RFM69_LED_TYPE_NONE = 0,
	RFM69_LED_TYPE_RX,
	RFM69_LED_TYPE_TX,
	RFM69_LED_TYPE_ALL,
};

struct module_rfm69 {
	struct hal_module_descriptor module;
	struct interface_netdev iface;

	struct interface_spidev *spidev;
	struct interface_led *tx_led;
	struct interface_led *rx_led;

	enum module_rfm69_state driver_state;
	//~ Semaphore tx_sem, rx_sem;
	SemaphoreHandle_t semaphore;

};


/******************************************************************************
 * Module public API
 ******************************************************************************/

int32_t module_rfm69_init(struct module_rfm69 *rfm69, const char *name, struct interface_spidev *spidev);
#define MODULE_RFM69_INIT_OK 0
#define MODULE_RFM69_INIT_FAILED -1

int32_t module_rfm69_free(struct module_rfm69 *rfm69);
#define MODULE_RFM69_FREE_OK 0
#define MODULE_RFM69_FREE_FAILED -1

/**
 * @brief General interrupt handler
 *
 * There are two different types of driver operation:
 * * interrupt based - a mapping between external interrupt and driver
 *   interrupt handler is required. Interrupts lines from RFM69 module
 *   are aggregated to single ISR (in hw or sw) and interrupt is served
 *   depending on the internal driver state. RFM69 interrupt source
 *   is being checked inside the driver task during transmission on
 *   reception operation.
 * * polled - no external interrupts are used and the driver interrupt
 *   handler is called periodically by a system virtual timer or general
 *   purpose timer (GPT). This approach causes more overhead, SPI traffic
 *   and also delays packet reception by some amount of time. Timer
 *   interval MUST be chosen shorter than the time required to transfer
 *   32 bytes over the air (16B is recommended to provide some margin)
 *
 * @param rfm69 Module instance to use.
 *
 * @return MODULE_RFM69_INTERRUPT_HANDLER_OK if the interrupt request was
 *                                           processed successfully or
 *         MODULE_RFM69_INTERRUPT_HANDLER_FAILED otherwise.
 */
int32_t module_rfm69_interrupt_handler(struct module_rfm69 *rfm69);
#define MODULE_RFM69_INTERRUPT_HANDLER_OK 0
#define MODULE_RFM69_INTERRUPT_HANDLER_FAILED -1

int32_t module_rfm69_set_led(struct module_rfm69 *rfm69, enum module_rfm69_led_type type, struct interface_led *led);
#define MODULE_RFM69_SET_LED_OK 0
#define MODULE_RFM69_SET_LED_FAILED -1

/**
 * @brief Get radio frequency.
 *
 * @param driver RFM69 driver instance
 * @param freq Frequency in kHz (pointer)
 */
int32_t module_rfm69_get_frequency(struct module_rfm69 *rfm69, int32_t *freq);
#define MODULE_RFM69_GET_FREQUENCY_OK 0
#define MODULE_RFM69_GET_FREQUENCY_FAILED -1

/**
 * @brief Set radio frequency and check if operation succeeded
 *
 * @param driver RFM69 driver instance
 * @param freq Frequency in kHz
 */
int32_t module_rfm69_set_frequency(struct module_rfm69 *rfm69, int32_t freq);
#define MODULE_RFM69_SET_FREQUENCY_OK 0
#define MODULE_RFM69_SET_FREQUENCY_FAILED -1

/**
 * @brief Get transmitter output power
 *
 * @param driver RFM69 driver instance
 * @param power Output power in dBm
 */
int32_t module_rfm69_get_txpower(struct module_rfm69 *rfm69, int32_t *power);
#define MODULE_RFM69_GET_TXPOWER_OK 0
#define MODULE_RFM69_GET_TXPOWER_FAILED -1

/**
 * @brief Set transmitter output power and check if operation succeeded
 *
 * @param driver RFM69 driver instance
 * @param power Output power in dBm
 */
int32_t module_rfm69_set_txpower(struct module_rfm69 *rfm69, int32_t power);
#define MODULE_RFM69_SET_TXPOWER_OK 0
#define MODULE_RFM69_SET_TXPOWER_FAILED -1


#endif

