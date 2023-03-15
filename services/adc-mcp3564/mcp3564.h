/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Microchip MCP3561/2/4 ADC driver
 *
 * Copyright (c) 2022, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <interfaces/spi.h>

typedef enum  {
	MCP3564_RET_OK = 0,
	MCP3564_RET_FAILED,
	MCP3564_RET_NOT_FOUND,
} mcp3564_ret_t;

enum mcp3564_command {
	MCP3564_CMD_NONE = 0x00,
	MCP3564_CMD_START = 0x28,
	MCP3564_CMD_STANDBY = 0x2c,
	MCP3564_CMD_SHUTDOWN = 0x30,
	MCP3564_CMD_FULL_SHUTDOWN = 0x34,
	MCP3564_CMD_FULL_RESET = 0x38,
	MCP3564_CMD_READ = 0x01,
	MCP3564_CMD_INCREMENTAL_WRITE = 0x02,
	MCP3564_CMD_INCREMENTAL_READ = 0x03,
};

enum mcp3564_reg {
	MCP3564_REG_ADCDATA = 0x0,
	MCP3564_REG_CONFIG0 = 0x1,
	MCP3564_REG_CONFIG1 = 0x2,
	MCP3564_REG_CONFIG2 = 0x3,
	MCP3564_REG_CONFIG3 = 0x4,
	MCP3564_REG_IRQ = 0x5,
	MCP3564_REG_MUX = 0x6,
	MCP3564_REG_SCAN = 0x7,
	MCP3564_REG_TIMER = 0x8,
	MCP3564_REG_OFFSETCAL = 0x9,
	MCP3564_REG_GAINCAL = 0xa,
	MCP3564_REG_MAX = 0xa,
	MCP3564_REG_LOCK = 0xd,
	MCP3564_REG_RESERVED_PN = 0xe,
	MCP3564_REG_CRCCFG = 0xf,
};

enum mcp3564_clock_sel {
	MCP3564_CLOCK_SEL_EXTERNAL = (0 << 4),
	MCP3564_CLOCK_SEL_INTERNAL = (2 << 4),
	MCP3564_CLOCK_SEL_INTERNAL_OUT = (3 << 4),
};

enum mcp3564_sensor_bias {
	MCP3564_SENSOR_BIAS_NONE = (0 << 2),
	MCP3564_SENSOR_BIAS_09 = (1 << 2),
	MCP3564_SENSOR_BIAS_37 = (2 << 2),
	MCP3564_SENSOR_BIAS_15 = (3 << 2),
};

enum mcp3564_mode {
	MCP3564_MODE_SHUTDOWN = (0 << 0),
	MCP3564_MODE_STANDBY = (2 << 0),
	MCP3564_MODE_CONVERSION = (3 << 0),
};

enum mcp3564_prescaler {
	MCP3564_PRESCALER_MCLK = (0 << 6),
	MCP3564_PRESCALER_DIV_2 = (1 << 6),
	MCP3564_PRESCALER_DIV_4 = (2 << 6),
	MCP3564_PRESCALER_DIV_8 = (3 << 6),
};

enum mcp3564_osr {
	MCP3564_OSR_32 = (0 << 2),
	MCP3564_OSR_64 = (1 << 2),
	MCP3564_OSR_128 = (2 << 2),
	MCP3564_OSR_256 = (3 << 2),
	MCP3564_OSR_512 = (4 << 2),
	MCP3564_OSR_1024 = (5 << 2),
	MCP3564_OSR_2048 = (6 << 2),
	MCP3564_OSR_4096 = (7 << 2),
	MCP3564_OSR_8192 = (8 << 2),
	MCP3564_OSR_16384 = (9 << 2),
	MCP3564_OSR_20480 = (10 << 2),
	MCP3564_OSR_24576 = (11 << 2),
	MCP3564_OSR_40960 = (12 << 2),
	MCP3564_OSR_49152 = (13 << 2),
	MCP3564_OSR_81920 = (14 << 2),
	MCP3564_OSR_98304 = (15 << 2),
	MCP3564_OSR_MASK = (15 << 2),
};

enum mcp3564_boost {
	MCP3564_BOOST_05 = (0 << 6),
	MCP3564_BOOST_066 = (1 << 6),
	MCP3564_BOOST_1 = (2 << 6),
	MCP3564_BOOST_2 = (3 << 6),
};

enum mcp3564_gain {
	MCP3564_GAIN_033 = (0 << 3),
	MCP3564_GAIN_1 = (1 << 3),
	MCP3564_GAIN_2 = (2 << 3),
	MCP3564_GAIN_4 = (3 << 3),
	MCP3564_GAIN_8 = (4 << 3),
	MCP3564_GAIN_16 = (5 << 3),
	MCP3564_GAIN_32 = (6 << 3),
	MCP3564_GAIN_64 = (7 << 3),
	MCP3564_GAIN_MASK = (7 << 3),
};


enum mcp3564_conversion_mode {
	MCP3564_CONVERESION_MODE_ONE_SHOT_SHUTDOWN = (0 << 6),
	MCP3564_CONVERESION_MODE_ONE_SHOT_STANDBY = (2 << 6),
	MCP3564_CONVERESION_MODE_CONTINUOUS = (3 << 6),
};

enum mcp3564_data_format {
	MCP3564_DATA_FORMAT_24_BIT = (0 << 4),
	MCP3564_DATA_FORMAT_32_BIT_LEFT = (1 << 4),
	MCP3564_DATA_FORMAT_32_BIT_RIGHT_SGN = (2 << 4),
	MCP3564_DATA_FORMAT_32_BIT_RIGHT_SGN_CH = (3 << 4),
};

enum mcp3564_reg_iqr {
	MCP3564_REG_IRQ_DR_STATUS = 0x40,
	MCP3564_REG_IRQ_CRCCFG_STATUS = 0x20,
	MCP3564_REG_IRQ_POR_STATUS = 0x10,
	MCP3564_REG_IRQ_MDAT = 0x08,
	MCP3564_REG_IRQ_INACTIVE = 0x04,
	MCP3564_REG_IRQ_EN_FASTCMD = 0x02,
	MCP3564_REG_IRQ_EN_STP = 0x01,
};

enum mcp3564_mux {
	MCP3564_MUX_CH0 = 0x0,
	MCP3564_MUX_CH1 = 0x1,
	MCP3564_MUX_CH2 = 0x2,
	MCP3564_MUX_CH3 = 0x3,
	MCP3564_MUX_CH4 = 0x4,
	MCP3564_MUX_CH5 = 0x5,
	MCP3564_MUX_CH6 = 0x6,
	MCP3564_MUX_CH7 = 0x7,
	MCP3564_MUX_AGND = 0x8,
	MCP3564_MUX_AVDD = 0x9,
	MCP3564_MUX_RES = 0xa,
	MCP3564_MUX_REFINP = 0xb,
	MCP3564_MUX_REFINM = 0xc,
	MCP3564_MUX_TEMPP = 0xd,
	MCP3564_MUX_TEMPM = 0xe,
	MCP3564_MUX_VCM = 0xf,
};

enum mcp3564_irq_mode {
	MCP3564_IRQ_MODE_MDAT,
	MCP3564_IRQ_MODE_IRQ,
};

enum mcp3564_irq_inactive {
	MCP3564_IRQ_INACTIVE_H,
	MCP3564_IRQ_INACTIVE_HIZ,
};



#define MCP3564_REG_DEFAULTS (uint32_t []){0, 0x00, 0x0c, 0x8b, 0x00, 0x77, 0x01, 0, 0, 0, 0x800000}
#define MCP3564_REG_SIZES (uint32_t []){4, 1, 1, 1, 1, 1, 1, 3, 3, 3, 3}

typedef struct {
	SpiDev *spidev;

	uint32_t regs[MCP3564_REG_MAX + 1];
} Mcp3564;




mcp3564_ret_t mcp3564_init(Mcp3564 *self, SpiDev *spidev);
mcp3564_ret_t mcp3564_send_cmd(Mcp3564 *self, enum mcp3564_command cmd, uint8_t *status, const uint8_t *txdata, size_t txlen, uint8_t *rxdata, size_t rxlen);
mcp3564_ret_t mcp3564_read_reg(Mcp3564 *self, enum mcp3564_reg reg, size_t bytes, uint32_t *value, uint8_t *status);
mcp3564_ret_t mcp3564_write_reg(Mcp3564 *self, enum mcp3564_reg reg, size_t bytes, uint32_t value, uint8_t *status);
mcp3564_ret_t mcp3564_update(Mcp3564 *self);
mcp3564_ret_t mcp3564_check(Mcp3564 *self);

/* CONFIG0 register related functions */
mcp3564_ret_t mcp3564_shutdown(Mcp3564 *self);
mcp3564_ret_t mcp3564_select_clock(Mcp3564 *self, enum mcp3564_clock_sel sel);
mcp3564_ret_t mcp3564_set_sensor_bias(Mcp3564 *self, enum mcp3564_sensor_bias bias);
mcp3564_ret_t mcp3564_set_mode(Mcp3564 *self, enum mcp3564_mode mode);

/* CONFIG1 register related functions */
mcp3564_ret_t mcp3564_set_prescaler(Mcp3564 *self, enum mcp3564_prescaler prescaler);
mcp3564_ret_t mcp3564_set_osr(Mcp3564 *self, enum mcp3564_osr osr);

/* CONFIG2 register related functions */
mcp3564_ret_t mcp3564_set_boost(Mcp3564 *self, enum mcp3564_boost boost);
mcp3564_ret_t mcp3564_set_gain(Mcp3564 *self, enum mcp3564_gain gain);
mcp3564_ret_t mcp3564_set_azmux(Mcp3564 *self, bool en);

/* CONFIG3 register related functions */
mcp3564_ret_t mcp3564_set_conversion_mode(Mcp3564 *self, enum mcp3564_conversion_mode mode);
mcp3564_ret_t mcp3564_set_data_format(Mcp3564 *self, enum mcp3564_data_format data_format);
mcp3564_ret_t mcp3564_set_pad_crc_enable(Mcp3564 *self, bool en);
mcp3564_ret_t mcp3564_set_offcal_enable(Mcp3564 *self, bool en);
mcp3564_ret_t mcp3564_set_gaincal_enable(Mcp3564 *self, bool en);

/* IRQ register related functions */
mcp3564_ret_t mcp3564_set_irq_mode(Mcp3564 *self, enum mcp3564_irq_mode mode);
mcp3564_ret_t mcp3564_set_irq_inactive(Mcp3564 *self, enum mcp3564_irq_inactive inactive);
mcp3564_ret_t mcp3564_set_fastcmd_enable(Mcp3564 *self, bool en);
mcp3564_ret_t mcp3564_set_stp_enable(Mcp3564 *self, bool en);

mcp3564_ret_t mcp3564_set_mux(Mcp3564 *self, enum mcp3564_mux muxp, enum mcp3564_mux muxm);

