/*
 * HopeRF RFM69CW/W/HW module driver
 *
 * Copyright (c) 2014-2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "interfaces/radio.h"
#include "interface_spidev.h"
#include "interface_led.h"


#define RFM69_XTAL_FREQ 32000000UL
#define RFM69_DEFAULT_TIMEOUT_TICS 2

typedef enum {
	RFM69_RET_OK = 0,
	RFM69_RET_NULL,
	RFM69_RET_BAD_ARG,
	RFM69_RET_FAILED,
} rfm69_ret_t;


/* List of registers from the SX1231 datasheet, Rev. 7 - June 2013. */
enum rfm69_register {
	RFM69_REG_FIFO = 0x00,
	RFM69_REG_OPMODE = 0x01,
	RFM69_REG_DATAMODUL = 0x02,
	RFM69_REG_BITRATEMSB = 0x03,
	RFM69_REG_BITRATELSB = 0x04,
	RFM69_REG_FDEVMSB = 0x05,
	RFM69_REG_FDEVLSB = 0x06,
	RFM69_REG_FRFMSB = 0x07,
	RFM69_REG_FRFMID = 0x08,
	RFM69_REG_FRFLSB = 0x09,
	RFM69_REG_OSC1 = 0x0A,
	RFM69_REG_AFCCTRL = 0x0B,
	RFM69_REG_LOWBAT = 0x0C,
	RFM69_REG_LISTEN1 = 0x0D,
	RFM69_REG_LISTEN2 = 0x0E,
	RFM69_REG_LISTEN3 = 0x0F,
	RFM69_REG_VERSION = 0x10,
	RFM69_REG_PALEVEL = 0x11,
	RFM69_REG_PARAMP = 0x12,
	RFM69_REG_OCP = 0x13,
	RFM69_REG_RESERVED14 = 0x14,
	RFM69_REG_RESERVED15 = 0x15,
	RFM69_REG_RESERVED16 = 0x16,
	RFM69_REG_RESERVED17 = 0x17,
	RFM69_REG_LNA = 0x18,
	RFM69_REG_RXBW = 0x19,
	RFM69_REG_AFCBW = 0x1A,
	RFM69_REG_OOKPEAK = 0x1B,
	RFM69_REG_OOKAVG = 0x1C,
	RFM69_REG_OOKFIX = 0x1D,
	RFM69_REG_AFCFEI = 0x1E,
	RFM69_REG_AFCMSB = 0x1F,
	RFM69_REG_AFCLSB = 0x20,
	RFM69_REG_FEIMSB = 0x21,
	RFM69_REG_FEILSB = 0x22,
	RFM69_REG_RSSICONFIG = 0x23,
	RFM69_REG_RSSIVALUE = 0x24,
	RFM69_REG_DIOMAPPING1 = 0x25,
	RFM69_REG_DIOMAPPING2 = 0x26,
	RFM69_REG_IRQFLAGS1 = 0x27,
	RFM69_REG_IRQFLAGS2 = 0x28,
	RFM69_REG_RSSITHRESH = 0x29,
	RFM69_REG_RXTIMEOUT1 = 0x2A,
	RFM69_REG_RXTIMEOUT2 = 0x2B,
	RFM69_REG_PREAMBLEMSB = 0x2C,
	RFM69_REG_PREAMBLELSB = 0x2D,
	RFM69_REG_SYNCCONFIG = 0x2E,
	RFM69_REG_SYNCVALUE = 0x2F,
	RFM69_REG_PACKETCONFIG1 = 0x37,
	RFM69_REG_PAYLOADLENGTH = 0x38,
	RFM69_REG_NODEADRS = 0x39,
	RFM69_REG_BROADCASTADRS = 0x3A,
	RFM69_REG_AUTOMODES = 0x3B,
	RFM69_REG_FIFOTHRESH = 0x3C,
	RFM69_REG_PACKETCONFIG2 = 0x3D,
	RFM69_REG_AESKEY = 0x3E,
	RFM69_REG_TEMP1 = 0x4E,
	RFM69_REG_TEMP2 = 0x4F,
	RFM69_REG_TESTLNA = 0x58,
	RFM69_REG_TESTTCXO = 0x59,
	RFM69_REG_TESTLLBW = 0x5F,
	RFM69_REG_TESTDAGC = 0x6F,
	RFM69_REG_TESTAFC = 0x71,
	RFM69_REG_TEST = 0x50,
};


enum rfm69_rstatus {
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

	RFM69_RSTATUS_INVALID =           0x0000,
};


enum rfm69_rmode {
	RFM69_RMODE_SLEEP = 0,
	RFM69_RMODE_STDBY = 1,
	RFM69_RMODE_FS =    2,
	RFM69_RMODE_TX =    3,
	RFM69_RMODE_RX =    4,
};


enum rfm69_state {
	RFM69_STATE_UNINITIALIZED = 0,
	RFM69_STATE_STOPPED,
	RFM69_STATE_IDLE,
	RFM69_STATE_RECEIVING,
	RFM69_STATE_TRANSMITTING
};


enum rfm69_led_type {
	RFM69_LED_TYPE_NONE = 0,
	RFM69_LED_TYPE_RX,
	RFM69_LED_TYPE_TX,
	RFM69_LED_TYPE_ALL,
};


typedef struct rfm69 {
	IRadio radio;

	/* Dependencies. */
	struct interface_spidev *spidev;
	struct interface_led *tx_led;
	struct interface_led *rx_led;

	enum rfm69_state driver_state;
	SemaphoreHandle_t semaphore;

	uint32_t bit_rate_bps;
} Rfm69;


rfm69_ret_t rfm69_init(Rfm69 *self, struct interface_spidev *spidev);
rfm69_ret_t rfm69_free(Rfm69 *self);

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
 */
rfm69_ret_t rfm69_interrupt_handler(Rfm69 *self);
rfm69_ret_t rfm69_set_led(Rfm69 *self, enum rfm69_led_type type, struct interface_led *led);
rfm69_ret_t rfm69_start(Rfm69 *self);
rfm69_ret_t rfm69_stop(Rfm69 *self);
