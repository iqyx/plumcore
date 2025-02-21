/* SPDX-License-Identifier: BSD-2-Clause
 *
 * STM32 SAI driver service
 *
 * Copyright (c) 2025, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <main.h>

#include <libopencm3/stm32/rcc.h>

#include <waveform-sink.h>


#define SAI1_ACR1               MMIO32(SAI1_BASE + 0x04)
#define SAI1_ACR2               MMIO32(SAI1_BASE + 0x08)
#define SAI1_AFRCR              MMIO32(SAI1_BASE + 0x0c)
#define SAI1_ASLOTR             MMIO32(SAI1_BASE + 0x10)
#define SAI1_AIM                MMIO32(SAI1_BASE + 0x14)
#define SAI1_ASR                MMIO32(SAI1_BASE + 0x18)
#define SAI1_ACLRFR             MMIO32(SAI1_BASE + 0x1c)
#define SAI1_ADR32              MMIO32(SAI1_BASE + 0x20)
#define SAI1_ADR16              MMIO16(SAI1_BASE + 0x20)
#define SAI1_APDMCR             MMIO32(SAI1_BASE + 0x44)
#define SAI1_APDMDLY            MMIO32(SAI1_BASE + 0x48)

#define SAI_CR1_MCKEN           (1 << 27)
#define SAI_CR1_MCKDIV_SHIFT    20
#define SAI_CR1_SAIEN           (1 << 16)
#define SAI_CR1_DS_SHIFT        5

#define SAI_AIM_OVRUDRIE        (1 << 0)
#define SAI_AIM_FREQIE          (1 << 3)

#define SAI_ASR_OVRUDR          (1 << 0)
#define SAI_ASR_FREQ            (1 << 3)

#define SAI_ACLRFR_COVRUDR      (1 << 0)


typedef enum {
	STM32_SAI_RET_OK = 0,
	STM32_SAI_RET_FAILED = -1,
} stm32_sai_ret_t;

typedef struct {
	WaveformSink sink;
	uint32_t dev;

} Stm32Sai;


stm32_sai_ret_t stm32_sai_init(Stm32Sai *self, uint32_t dev);
stm32_sai_ret_t stm32_sai_free(Stm32Sai *self);
stm32_sai_ret_t stm32_sai_irq_handler(Stm32Sai *self);

