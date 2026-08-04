/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments LP586x LED matrix driver
 *
 * References:
 *   - LP5862 datasheet SNVSC53: https://www.ti.com/lit/ds/symlink/lp5862.pdf
 *   - LP5860 datasheet SNVSC10: https://www.ti.com/lit/ds/symlink/lp5860.pdf
 *   - LP5860 register map SNVU786 (detailed register bit fields)
 *   - lp586x-rs reference driver (register layout cross-check): https://github.com/markus-k/lp586x-rs
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#include <main.h>

#include <interfaces/i2c-bus.h>
#include <interfaces/pwm.h>

#include "lp586x-led.h"

#define MODULE_NAME "lp586x-led"

/* The 18 constant current sinks (CS0..CS17) are shared by all parts of the family. The parts differ
 * only in the number of scan lines they multiplex, hence in the total LED dot count. */
#define LP586X_CS_COUNT 18


/***************************************************************************************************
 * Device register map (LP586x, datasheet SNVSC53 / LP5860 register map SNVU786)
 *
 * The register address is 10 bits wide. The two most significant bits (RA9:RA8) are transmitted in
 * the low two bits of the I2C address byte, the remaining eight bits (RA7:RA0) form the second byte
 * of the frame. See lp586x_i2c_addr().
 ***************************************************************************************************/

#define LP586X_REG_CHIP_EN            0x000
#define LP586X_CHIP_EN_ENABLE         (1 << 0)

/* Device initialization: scan line count, data refresh mode and PWM frequency. Max_Line_Num holds the
 * number of active scan lines (1..11) in bits [6:3]. Data_Ref_Mode in bits [2:1] selects mode 1 (0h,
 * 8-bit PWM updated instantly without a VSYNC command), which lets a PWM register write take effect
 * immediately. PWM_Fre in bit 0 is left at 0 (125 kHz). */
#define LP586X_REG_DEV_INITIAL        0x001
#define LP586X_MAX_LINE_NUM_SHIFT     3
#define LP586X_DATA_REF_MODE_1        0x00

/* Software reset: writing FFh resets all registers to their power-on defaults. The device retains its
 * entire register state across an MCU reset (only a power cycle resets it), so without this a stale
 * configuration left by a previous run - an incompatible data refresh mode, a disabled dot, zero
 * brightness - would survive and keep the outputs dark even though every register write succeeds. The
 * reset also returns the fault removal actions to their disabled default and clears latched faults. */
#define LP586X_REG_RESET              0x0a9
#define LP586X_RESET_VALUE            0xff
#define LP586X_RESET_DELAY_MS         5

/* Per-dot manual analog (DC) current and 8-bit PWM duty register banks (data refresh mode 1). Each
 * bank has one contiguous register per LED dot, indexed in scan order (dot = scan_line * 18 + CS). */
#define LP586X_REG_DC_BASE            0x100
#define LP586X_REG_PWM_BASE           0x200

#define LP586X_REG_DEV_CONFIG3        0x004
#define LP586X_MC_SHIFT               1


/* Compose the 7-bit I2C address byte for a given register: five bits of the device chip address
 * followed by the two most significant bits of the register address. */
static uint8_t lp586x_i2c_addr(Lp586x *self, uint16_t reg) {
	return ((self->conf.addr & 0x1f) << 2) | ((reg >> 8) & 0x03);
}


static lp586x_ret_t lp586x_write_reg(Lp586x *self, uint16_t reg, uint8_t val) {
	uint8_t txdata[2] = {(uint8_t)(reg & 0xff), val};
	if (self->conf.i2c->vmt->transfer(self->conf.i2c, lp586x_i2c_addr(self, reg), txdata, sizeof(txdata), NULL, 0) != I2C_BUS_RET_OK) {
		return LP586X_RET_FAILED;
	}
	return LP586X_RET_OK;
}


static lp586x_ret_t lp586x_read_reg(Lp586x *self, uint16_t reg, uint8_t *val) {
	uint8_t txdata[1] = {(uint8_t)(reg & 0xff)};
	if (self->conf.i2c->vmt->transfer(self->conf.i2c, lp586x_i2c_addr(self, reg), txdata, sizeof(txdata), val, sizeof(uint8_t)) != I2C_BUS_RET_OK) {
		return LP586X_RET_FAILED;
	}
	return LP586X_RET_OK;
}


/* Register of a dot's manual DC current / 8-bit PWM duty. */
static uint16_t lp586x_dc_reg(Lp586x *self, size_t channel) {
	(void)self;
	return LP586X_REG_DC_BASE + channel;
}


static uint16_t lp586x_pwm_reg(Lp586x *self, size_t channel) {
	(void)self;
	return LP586X_REG_PWM_BASE + channel;
}


/***************************************************************************************************
 * Pwm interface API
 *
 * The LED dots are laid out as a contiguous array, so the dot index is recovered directly from the
 * position of the Pwm interface within it.
 ***************************************************************************************************/

static pwm_ret_t pwm_set_pwm(Pwm *pwm, float duty) {
	Lp586x *self = pwm->parent;
	size_t ch = pwm - &self->channel[0];

	if (duty < 0.0f) {
		duty = 0.0f;
	}
	if (duty > 1.0f) {
		duty = 1.0f;
	}
	if (lp586x_write_reg(self, lp586x_pwm_reg(self, ch), (uint8_t)(duty * 255.0f + 0.5f)) != LP586X_RET_OK) {
		return PWM_RET_FAILED;
	}

	return PWM_RET_OK;
}


static pwm_ret_t pwm_get_pwm(Pwm *pwm, float *duty) {
	Lp586x *self = pwm->parent;
	size_t ch = pwm - &self->channel[0];

	if (duty == NULL) {
		return PWM_RET_FAILED;
	}
	uint8_t val = 0;
	if (lp586x_read_reg(self, lp586x_pwm_reg(self, ch), &val) != LP586X_RET_OK) {
		return PWM_RET_FAILED;
	}
	*duty = (float)val / 255.0f;

	return PWM_RET_OK;
}


static pwm_ret_t pwm_set_freq(Pwm *pwm, uint32_t freq_hz) {
	(void)pwm;
	(void)freq_hz;
	/* The PWM frequency is a device global configuration option, not an arbitrary per-channel
	 * setting, hence it is not exposed through the per-channel interface. */
	return PWM_RET_FAILED;
}


static pwm_ret_t pwm_get_freq(Pwm *pwm, uint32_t *freq_hz) {
	(void)pwm;
	(void)freq_hz;
	return PWM_RET_FAILED;
}


static const struct pwm_vmt lp586x_pwm_vmt = {
	.set_pwm = pwm_set_pwm,
	.get_pwm = pwm_get_pwm,
	.set_freq = pwm_set_freq,
	.get_freq = pwm_get_freq,
};


/***************************************************************************************************
 * Service implementation
 ***************************************************************************************************/

/* Number of active scan lines of a device type. The LED dot count is 18 (the constant current sink
 * count) times the number of scan lines. */
static lp586x_ret_t lp586x_device_lines(lp586x_type_t type, uint8_t *lines) {
	switch (type) {
		case LP586X_TYPE_LP5861:
			*lines = 1;
			return LP586X_RET_OK;
		case LP586X_TYPE_LP5862:
			*lines = 2;
			return LP586X_RET_OK;
		case LP586X_TYPE_LP5864:
			*lines = 4;
			return LP586X_RET_OK;
		case LP586X_TYPE_LP5866:
			*lines = 6;
			return LP586X_RET_OK;
		case LP586X_TYPE_LP5868:
			*lines = 8;
			return LP586X_RET_OK;
		case LP586X_TYPE_LP5860:
			*lines = 11;
			return LP586X_RET_OK;
		default:
			return LP586X_RET_FAILED;
	}
}


static const char *lp586x_device_name(lp586x_type_t type) {
	switch (type) {
		case LP586X_TYPE_LP5861:
			return "LP5861";
		case LP586X_TYPE_LP5862:
			return "LP5862";
		case LP586X_TYPE_LP5864:
			return "LP5864";
		case LP586X_TYPE_LP5866:
			return "LP5866";
		case LP586X_TYPE_LP5868:
			return "LP5868";
		case LP586X_TYPE_LP5860:
			return "LP5860";
		default:
			return "unknown";
	}
}


lp586x_ret_t lp586x_init(Lp586x *self, const struct lp586x_conf *conf) {
	memset(self, 0, sizeof(Lp586x));
	memcpy(&self->conf, conf, sizeof(struct lp586x_conf));

	uint8_t lines = 0;
	if (lp586x_device_lines(self->conf.type, &lines) != LP586X_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unknown device type"));
		return LP586X_RET_FAILED;
	}
	self->channel_count = (size_t)lines * LP586X_CS_COUNT;

	/* Dynamically allocate the array of per-dot PWM interfaces. */
	self->channel = malloc(self->channel_count * sizeof(Pwm));
	if (self->channel == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate %u PWM channels"), self->channel_count);
		return LP586X_RET_FAILED;
	}
	for (size_t i = 0; i < self->channel_count; i++) {
		self->channel[i].parent = self;
		self->channel[i].vmt = &lp586x_pwm_vmt;
	}

	/* Reset the device to a known state, so the driver does not depend on register values left behind by
	 * a previous run (the device retains them across an MCU reset). All registers this init relies on
	 * being at their defaults - global and group brightness, dot enable, colour current - are only known
	 * good after the reset. */
	if (lp586x_write_reg(self, LP586X_REG_RESET, LP586X_RESET_VALUE) != LP586X_RET_OK) {
		goto err;
	}
	vTaskDelay(pdMS_TO_TICKS(LP586X_RESET_DELAY_MS));

	/* Enable the chip, moving it from standby into normal mode. */
	if (lp586x_write_reg(self, LP586X_REG_CHIP_EN, LP586X_CHIP_EN_ENABLE) != LP586X_RET_OK) {
		goto err;
	}

	/* Set 51 mA maximum current. */
	if (lp586x_write_reg(self, LP586X_REG_DEV_CONFIG3, 0x47 | (7 << LP586X_MC_SHIFT)) != LP586X_RET_OK) {
		goto err;
	}

	/* Configure the scan line count and data refresh mode 1 while the device is still in standby
	 * (Chip_EN is 0 after the reset). In mode 1 a PWM register write is displayed instantly, no VSYNC
	 * needed. The config is latched when the chip is enabled below. */
	if (lp586x_write_reg(self, LP586X_REG_DEV_INITIAL,
	                     (lines << LP586X_MAX_LINE_NUM_SHIFT) | (LP586X_DATA_REF_MODE_1 << 1)) != LP586X_RET_OK) {
		goto err;
	}

	/* Set every dot to full analog (DC) current and zero PWM (dark), so that the per-dot PWM duty alone
	 * controls the brightness afterwards. The dots are enabled (Dot_onoff) by default. */
	for (size_t i = 0; i < self->channel_count; i++) {
		if (lp586x_write_reg(self, lp586x_dc_reg(self, i), 0x80) != LP586X_RET_OK ||
		    lp586x_write_reg(self, lp586x_pwm_reg(self, i), 0x00) != LP586X_RET_OK) {
			goto err;
		}
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized %s at 0x%02x, %u PWM channels"),
		lp586x_device_name(self->conf.type),
		self->conf.addr,
		self->channel_count
	);
	return LP586X_RET_OK;

err:
	free(self->channel);
	self->channel = NULL;
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("initialization failed"));
	return LP586X_RET_FAILED;
}


lp586x_ret_t lp586x_free(Lp586x *self) {
	if (self->channel != NULL) {
		free(self->channel);
		self->channel = NULL;
	}
	self->channel_count = 0;

	return LP586X_RET_OK;
}


lp586x_ret_t lp586x_get_pwm(Lp586x *self, size_t channel, Pwm **pwm) {
	if (pwm == NULL || channel >= self->channel_count) {
		return LP586X_RET_FAILED;
	}
	*pwm = &self->channel[channel];

	return LP586X_RET_OK;
}


lp586x_ret_t lp586x_set_max_current(Lp586x *self, size_t channel, float max) {
	if (channel >= self->channel_count) {
		return LP586X_RET_FAILED;
	}
	if (max < 0.0f) {
		max = 0.0f;
	}
	if (max > 1.0f) {
		max = 1.0f;
	}
	if (lp586x_write_reg(self, lp586x_dc_reg(self, channel), (uint8_t)(max * 255.0f + 0.5f)) != LP586X_RET_OK) {
		return LP586X_RET_FAILED;
	}

	return LP586X_RET_OK;
}
