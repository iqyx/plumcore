/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * STM32H7 clock tree
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <main.h>
#include <stm32h7xx.h>

#include "stm32-h7-clock.h"


stm32_clock_ret_t stm32_h7_clock_init(Stm32Clock *self) {
	memset(self, 0, sizeof(Stm32Clock));

	u_log(system_log, LOG_TYPE_INFO, "flash latency %u cycles", FLASH->ACR & 0xf);

	/***************************************************************************************************************
	 * 16 MHz HSE external oscillator.
	 **************************************************************************************************************/
	if (stm32_clock_osc_init(&self->hse_ck, &(const struct stm32_clock_osc_config){
		.name = "hse_ck",
		.fixed_rate_hz = 16000000ul,
		.reg_enable = {.reg = &((RCC_TypeDef *)RCC)->CR, .mask = RCC_CR_HSEON},
		.reg_ready  = {.reg = &((RCC_TypeDef *)RCC)->CR, .mask = RCC_CR_HSERDY},
	}) != STM32_CLOCK_RET_OK) {
		goto err;
	}

	/***************************************************************************************************************
	 * 64 MHz HSI internal RC oscillator.
	 **************************************************************************************************************/
	if (stm32_clock_osc_init(&self->hsi_ck, &(const struct stm32_clock_osc_config){
		.name = "hsi_ck",
		.fixed_rate_hz = 64000000ul,
		.reg_enable = {.reg = &((RCC_TypeDef *)RCC)->CR, .mask = RCC_CR_HSION},
		.reg_ready  = {.reg = &((RCC_TypeDef *)RCC)->CR, .mask = RCC_CR_HSIRDY},
	}) != STM32_CLOCK_RET_OK) {
		goto err;
	}

	/***************************************************************************************************************
	 * 4 MHz CSI internal RC oscillator.
	 **************************************************************************************************************/
	if (stm32_clock_osc_init(&self->csi_ck, &(const struct stm32_clock_osc_config){
		.name = "csi_ck",
		.fixed_rate_hz = 4000000ul,
		.reg_enable = {.reg = &((RCC_TypeDef *)RCC)->CR, .mask = RCC_CR_CSION},
		.reg_ready  = {.reg = &((RCC_TypeDef *)RCC)->CR, .mask = RCC_CR_CSIRDY},
	}) != STM32_CLOCK_RET_OK) {
		goto err;
	}

	/***************************************************************************************************************
	 * PLL-SRC mux, select HSE as the source.
	 **************************************************************************************************************/
	stm32_clock_mux_init(&self->pll_src_mux, &(const struct stm32_clock_mux_config){
		.name = "pll-src",
		.inputs = {
			&self->hsi_ck.clock,
			&self->csi_ck.clock,
			&self->hse_ck.clock,
		},
		.input_count = 3,
		.reg = &((RCC_TypeDef *)RCC)->PLLCKSELR,
		.mask = 0x00000003ul,
		.shift = 0,
		.disabled_id = 3,
	});
	self->pll_src_mux.clock.vmt->set_parent(&self->pll_src_mux.clock, &self->hse_ck.clock);

	/***************************************************************************************************************
	 * DIVM1 divider
	 **************************************************************************************************************/
	stm32_clock_div_init(&self->pll1m_div, &(const struct stm32_clock_div_config){
		.name = "pll1m-div",
		.parent = &self->pll_src_mux.clock,
		.reg_div = {.reg = &((RCC_TypeDef *)RCC)->PLLCKSELR, .mask = 0x000003f0ul, .shift = 4},
		.offset_div = 0,
		.min_rate_hz = 1000000ul,
		.max_rate_hz = 16000000ul,
		.min_prescaler = 1,
		.max_prescaler = 63,
	});
	self->pll1m_div.clock.vmt->set_rate(&self->pll1m_div.clock, 1000000ul);

	/***************************************************************************************************************
	 * PLL1N multiplier (VCO): 4 MHz input × 80 = 400 MHz VCO
	 **************************************************************************************************************/
	stm32_clock_mul_init(&self->pll1n_mul, &(const struct stm32_clock_mul_config){
		.name = "pll1n-mul",
		.parent = &self->pll1m_div.clock,
		.reg_plln = {.reg = &((RCC_TypeDef *)RCC)->PLL1DIVR, .mask = RCC_PLL1DIVR_N1_Msk, .shift = RCC_PLL1DIVR_N1_Pos},
		.offset_plln = 1,
		.reg_enable = {.reg = &((RCC_TypeDef *)RCC)->CR, .mask = RCC_CR_PLL1ON, .shift = RCC_CR_PLL1ON_Pos},
		.reg_ready = {.reg = &((RCC_TypeDef *)RCC)->CR, .mask = RCC_CR_PLL1RDY, .shift = RCC_CR_PLL1RDY_Pos},
		.min_multiplier = 4,
		.max_multiplier = 512,
		.min_rate_hz = 192000000ul,
		.max_rate_hz = 960000000ul,
	});
	self->pll1n_mul.clock.vmt->set_rate(&self->pll1n_mul.clock, 256000000ul);

	/***************************************************************************************************************
	 * PLL1P divider: 320 MHz VCO → 64 MHz
	 **************************************************************************************************************/
	stm32_clock_div_init(&self->pll1p_div, &(const struct stm32_clock_div_config){
		.name = "pll1p-div",
		.parent = &self->pll1n_mul.clock,
		.reg_div = {.reg = &((RCC_TypeDef *)RCC)->PLL1DIVR, .mask = RCC_PLL1DIVR_P1, .shift = RCC_PLL1DIVR_P1_Pos},
		.offset_div = 1,
		.min_rate_hz = 1000000ul,
		.max_rate_hz = 400000000ul,
		.min_prescaler = 2,
		.max_prescaler = 128,
	});
	self->pll1p_div.clock.vmt->set_rate(&self->pll1p_div.clock, 128000000ul);

	/***************************************************************************************************************
	 * PLL1P gate: controls DIVP1EN in RCC_PLLCFGR
	 **************************************************************************************************************/
	stm32_clock_gate_init(&self->pll1p_gate, &(const struct stm32_clock_gate_config){
		.name = "pll1p-gate",
		.parent = &self->pll1p_div.clock,
		.reg_enable = {.reg = &((RCC_TypeDef *)RCC)->PLLCFGR, .mask = RCC_PLLCFGR_DIVP1EN, .shift = RCC_PLLCFGR_DIVP1EN_Pos},
	});

	/***************************************************************************************************************
	 * SYS-CK mux
	 **************************************************************************************************************/
	stm32_clock_mux_init(&self->sys_ck_mux, &(const struct stm32_clock_mux_config){
		.name = "sys-ck",
		.inputs = {
			&self->hsi_ck.clock,
			&self->csi_ck.clock,
			&self->hse_ck.clock,
			&self->pll1p_gate.clock,
		},
		.input_count = 4,
		.reg = &((RCC_TypeDef *)RCC)->CFGR,
		.mask = 0x00000007ul,
		.shift = 0,
		.disabled_id = 0,
	});
	self->sys_ck_mux.clock.vmt->set_parent(&self->sys_ck_mux.clock, &self->pll1p_gate.clock);
	self->sys_ck_mux.clock.vmt->enable(&self->sys_ck_mux.clock);

	return STM32_CLOCK_RET_OK;
err:
	return STM32_CLOCK_RET_FAILED;
}


stm32_clock_ret_t stm32_h7_clock_free(Stm32Clock *self) {
	(void)self;
	return STM32_CLOCK_RET_OK;
}
