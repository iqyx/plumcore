/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Texas Instruments LP5810/LP5812 LED driver
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

#include "lp581x-led.h"

#define MODULE_NAME "lp581x-led"


/***************************************************************************************************
 * Device register map (LP5810/LP5812, datasheet SNVSCD0)
 *
 * The register address is 10 bits wide. The two most significant bits (RA9:RA8) are transmitted in
 * the low two bits of the I2C address byte, the remaining eight bits (RA7:RA0) form the second byte
 * of the frame. See lp581x_i2c_addr().
 ***************************************************************************************************/

#define LP581X_REG_CHIP_EN            0x000
#define LP581X_CHIP_EN_ENABLE         (1 << 0)

#define LP581X_REG_DEV_CONFIG1        0x002
#define LP581X_LED_MODE_4_SCAN        0x40   /* led_mode = 4h: TCM scan drive mode, 4 scans */

/* LED fault detection configuration. lsd_threshold = 3h is the datasheet-recommended value to avoid
 * incorrect LED-short detection; both fault actions (lod_action, lsd_action) are left disabled so that a
 * spurious open/short detection only sets a status flag and never latches an output off. The status LEDs
 * are driven at a low per-dot current, where the open/short detectors are prone to false triggering. */
#define LP581X_REG_DEV_CONFIG12       0x00d
#define LP581X_DEV_CONFIG12_VALUE     0x03

#define LP581X_REG_UPDATE_CMD         0x010
#define LP581X_UPDATE_CMD             0x55

/* Fault clear register: writing a 1 to tsd_clear/lod_clear/lsd_clear clears the corresponding latched
 * fault. The device retains its faults across an MCU reset (only a power cycle resets it), so a fault
 * latched in a previous run has to be cleared explicitly during initialization. */
#define LP581X_REG_FAULT_CLEAR        0x022
#define LP581X_FAULT_CLEAR_ALL        0x07

/* LED enable bitmap. Each bit enables one output; LED_EN1 covers outputs 0..7, LED_EN2 (0x021)
 * covers outputs 8..11 on the 12 channel parts. */
#define LP581X_REG_LED_EN1            0x020

/* Per-output manual analog (DC) current and PWM duty register banks (manual mode is the device
 * default). The channel index is offset by the device channel base (see lp581x_dc_reg/pwm_reg). */
#define LP581X_REG_DC_BASE            0x030
#define LP581X_REG_PWM_BASE           0x040


/* Compose the 7-bit I2C address byte for a given register: five bits of the device chip address
 * followed by the two most significant bits of the register address. */
static uint8_t lp581x_i2c_addr(Lp581x *self, uint16_t reg) {
	return ((self->addr & 0x1f) << 2) | ((reg >> 8) & 0x03);
}


static lp581x_ret_t lp581x_write_reg(Lp581x *self, uint16_t reg, uint8_t val) {
	uint8_t txdata[2] = {(uint8_t)(reg & 0xff), val};
	if (self->i2c->vmt->transfer(self->i2c, lp581x_i2c_addr(self, reg), txdata, sizeof(txdata), NULL, 0) != I2C_BUS_RET_OK) {
		return LP581X_RET_FAILED;
	}
	return LP581X_RET_OK;
}


static lp581x_ret_t lp581x_read_reg(Lp581x *self, uint16_t reg, uint8_t *val) {
	uint8_t txdata[1] = {(uint8_t)(reg & 0xff)};
	if (self->i2c->vmt->transfer(self->i2c, lp581x_i2c_addr(self, reg), txdata, sizeof(txdata), val, sizeof(uint8_t)) != I2C_BUS_RET_OK) {
		return LP581X_RET_FAILED;
	}
	return LP581X_RET_OK;
}


/* Register of a channel's manual DC current / PWM duty, accounting for the device channel base. */
static uint16_t lp581x_dc_reg(Lp581x *self, size_t channel) {
	return LP581X_REG_DC_BASE + self->channel_base + channel;
}


static uint16_t lp581x_pwm_reg(Lp581x *self, size_t channel) {
	return LP581X_REG_PWM_BASE + self->channel_base + channel;
}


/***************************************************************************************************
 * Pwm interface API
 *
 * The output channels are laid out as a contiguous array, so the channel index is recovered
 * directly from the position of the Pwm interface within it.
 ***************************************************************************************************/

static pwm_ret_t pwm_set_pwm(Pwm *pwm, float duty) {
	Lp581x *self = pwm->parent;
	size_t ch = pwm - &self->channel[0];

	if (duty < 0.0f) {
		duty = 0.0f;
	}
	if (duty > 1.0f) {
		duty = 1.0f;
	}
	if (lp581x_write_reg(self, lp581x_pwm_reg(self, ch), (uint8_t)(duty * 255.0f + 0.5f)) != LP581X_RET_OK) {
		return PWM_RET_FAILED;
	}

	return PWM_RET_OK;
}


static pwm_ret_t pwm_get_pwm(Pwm *pwm, float *duty) {
	Lp581x *self = pwm->parent;
	size_t ch = pwm - &self->channel[0];

	if (duty == NULL) {
		return PWM_RET_FAILED;
	}
	uint8_t val = 0;
	if (lp581x_read_reg(self, lp581x_pwm_reg(self, ch), &val) != LP581X_RET_OK) {
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


static const struct pwm_vmt lp581x_pwm_vmt = {
	.set_pwm = pwm_set_pwm,
	.get_pwm = pwm_get_pwm,
	.set_freq = pwm_set_freq,
	.get_freq = pwm_get_freq,
};


/***************************************************************************************************
 * Service implementation
 ***************************************************************************************************/

static lp581x_ret_t lp581x_device_params(lp581x_type_t type, size_t *count, uint8_t *base) {
	switch (type) {
		case LP581X_TYPE_LP5810:
			/* Four direct-drive outputs. */
			*count = 4;
			*base = 0;
			return LP581X_RET_OK;
		case LP581X_TYPE_LP5812:
			/* Twelve outputs driven through the 4x3 scan matrix, whose registers and enable bits
			 * follow the four (unused) direct-drive outputs. */
			*count = 12;
			*base = 4;
			return LP581X_RET_OK;
		default:
			return LP581X_RET_FAILED;
	}
}


lp581x_ret_t lp581x_init(Lp581x *self, I2cBus *i2c, uint8_t addr, lp581x_type_t type) {
	memset(self, 0, sizeof(Lp581x));
	self->i2c = i2c;
	self->addr = addr;
	self->type = type;
	if (lp581x_device_params(type, &self->channel_count, &self->channel_base) != LP581X_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("unknown device type"));
		return LP581X_RET_FAILED;
	}

	/* Dynamically allocate the array of per-output PWM interfaces. */
	self->channel = malloc(self->channel_count * sizeof(Pwm));
	if (self->channel == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate %u PWM channels"), self->channel_count);
		return LP581X_RET_FAILED;
	}
	for (size_t i = 0; i < self->channel_count; i++) {
		self->channel[i].parent = self;
		self->channel[i].vmt = &lp581x_pwm_vmt;
	}

	/* Enable the chip. */
	if (lp581x_write_reg(self, LP581X_REG_CHIP_EN, LP581X_CHIP_EN_ENABLE) != LP581X_RET_OK) {
		goto err;
	}

	/* The 12-channel parts drive their LEDs through the scan matrix, which has to be switched into
	 * the 4-scan TCM mode (with the default OUT0..OUT3 scan order). This is a CONFIG register, so it
	 * only takes effect after the update command. The 4-channel parts use direct drive by default,
	 * and per-channel manual DC/PWM control is the default in both cases. */
	if (self->type == LP581X_TYPE_LP5812) {
		if (lp581x_write_reg(self, LP581X_REG_DEV_CONFIG1, LP581X_LED_MODE_4_SCAN) != LP581X_RET_OK) {
			goto err;
		}
	}

	/* Configure the fault detection (a CONFIG register too) and latch all CONFIG registers with the update
	 * command. Without this the fault actions default to on, so a spurious open/short detection latches the
	 * affected output off until the device is power-cycled. */
	if (lp581x_write_reg(self, LP581X_REG_DEV_CONFIG12, LP581X_DEV_CONFIG12_VALUE) != LP581X_RET_OK ||
	    lp581x_write_reg(self, LP581X_REG_UPDATE_CMD, LP581X_UPDATE_CMD) != LP581X_RET_OK) {
		goto err;
	}

	/* Clear any fault flags left latched by a previous run, which the device retains across an MCU reset. */
	if (lp581x_write_reg(self, LP581X_REG_FAULT_CLEAR, LP581X_FAULT_CLEAR_ALL) != LP581X_RET_OK) {
		goto err;
	}

	/* Enable the driven LED outputs. The enable bitmap spans one register per eight outputs (LED_EN1
	 * at 0x020, LED_EN2 at 0x021) and starts at the device channel base. */
	uint32_t en_mask = ((1UL << self->channel_count) - 1) << self->channel_base;
	for (size_t i = 0; i * 8 < (size_t)(self->channel_base + self->channel_count); i++) {
		if (lp581x_write_reg(self, LP581X_REG_LED_EN1 + i, (en_mask >> (i * 8)) & 0xff) != LP581X_RET_OK) {
			goto err;
		}
	}

	/* Set every output to full analog (DC) current and zero PWM (dark), so that the per-channel
	 * PWM duty alone controls the brightness afterwards. */
	for (size_t i = 0; i < self->channel_count; i++) {
		if (lp581x_write_reg(self, lp581x_dc_reg(self, i), 0xff) != LP581X_RET_OK ||
		    lp581x_write_reg(self, lp581x_pwm_reg(self, i), 0x00) != LP581X_RET_OK) {
			goto err;
		}
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized %s at 0x%02x, %u PWM channels"),
		(type == LP581X_TYPE_LP5810) ? "LP5810" : "LP5812",
		self->addr,
		self->channel_count
	);
	return LP581X_RET_OK;

err:
	free(self->channel);
	self->channel = NULL;
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("initialization failed"));
	return LP581X_RET_FAILED;
}


lp581x_ret_t lp581x_free(Lp581x *self) {
	if (self->channel != NULL) {
		free(self->channel);
		self->channel = NULL;
	}
	self->channel_count = 0;

	return LP581X_RET_OK;
}


lp581x_ret_t lp581x_get_pwm(Lp581x *self, size_t channel, Pwm **pwm) {
	if (pwm == NULL || channel >= self->channel_count) {
		return LP581X_RET_FAILED;
	}
	*pwm = &self->channel[channel];

	return LP581X_RET_OK;
}


lp581x_ret_t lp581x_set_max_current(Lp581x *self, size_t channel, float max) {
	if (channel >= self->channel_count) {
		return LP581X_RET_FAILED;
	}
	if (max < 0.0f) {
		max = 0.0f;
	}
	if (max > 1.0f) {
		max = 1.0f;
	}
	if (lp581x_write_reg(self, lp581x_dc_reg(self, channel), (uint8_t)(max * 255.0f + 0.5f)) != LP581X_RET_OK) {
		return LP581X_RET_FAILED;
	}

	return LP581X_RET_OK;
}
