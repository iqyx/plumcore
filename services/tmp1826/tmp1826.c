/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * TMP1826 1-Wire temperature sensor and EEPROM driver
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <main.h>

#include <interfaces/uart.h>
#include <interfaces/stream.h>
#include <interfaces/flash.h>
#include <interfaces/sensor.h>

#include "tmp1826.h"

#define MODULE_NAME "tmp1826"


/* 1-Wire-over-UART bitrates: the reset/presence slot is generated at 9600 baud (the framing start bit plus the low
 * data bits of 0xF0 form the ~520 us reset low pulse) while each bit slot is generated at 115200 baud (one UART byte
 * per 1-Wire bit). */
#define TMP1826_OW_RESET_BAUD 9600
#define TMP1826_OW_DATA_BAUD 115200

/* Stream read timeout for the echoed bytes of a single transfer. Generous compared to the on-wire time. */
#define TMP1826_IO_TIMEOUT_MS 20

/* Worst case active conversion time is ~6 ms (CONV_TIME_SEL = 5.5 ms); keep a margin. */
#define TMP1826_CONV_TIME_MS 12

/* EEPROM 8-byte word programming time, tPROG max = 21 ms; keep a margin. */
#define TMP1826_PROG_TIME_MS 25

/* Idle bus time the device needs after the read address before it streams a block, tREADIDLE = 560 us. */
#define TMP1826_READ_IDLE_MS 1

/* Largest 1-Wire transfer performed in a single UART burst (in 1-Wire bytes). Reading scratchpad-1 needs 9 bytes;
 * each 1-Wire byte expands to 8 UART bytes, so this stays well within the UART rx buffer. */
#define TMP1826_OW_MAX_BYTES 16

/* ROM commands. */
#define TMP1826_ROM_READ 0x33
#define TMP1826_ROM_MATCH 0x55
#define TMP1826_ROM_SKIP 0xcc

/* Function commands. */
#define TMP1826_FUNC_CONVERT_TEMP 0x44
#define TMP1826_FUNC_WRITE_SCRATCHPAD1 0x4e
#define TMP1826_FUNC_COPY_SCRATCHPAD1 0x48
#define TMP1826_FUNC_READ_SCRATCHPAD1 0xbe
#define TMP1826_FUNC_WRITE_SCRATCHPAD2 0x0f
#define TMP1826_FUNC_READ_SCRATCHPAD2 0xaa
#define TMP1826_FUNC_COPY_SCRATCHPAD2 0x55
#define TMP1826_FUNC_READ_EEPROM 0xf0

/* Data byte the host sends to commit scratchpad-2 to the EEPROM during COPY SCRATCHPAD-2. */
#define TMP1826_COPY_COMMIT 0xa5

/* TEMP_FMT bit in the device configuration-1 register (scratchpad-1 byte 4): 0 = legacy 12-bit, 1 = high precision. */
#define TMP1826_CONFIG1_TEMP_FMT 0x80

/* On-chip user EEPROM geometry: 256 bytes organized as 8-byte blocks (the smallest accessible unit). */
#define TMP1826_EEPROM_SIZE 256
#define TMP1826_EEPROM_BLOCK 8


/**********************************************************************************************************************
 * 1-Wire link layer over the UART
 **********************************************************************************************************************/

/* Dallas/Maxim 1-Wire CRC-8 (polynomial x^8 + x^5 + x^4 + 1, reflected as 0x8c, initial value 0x00). */
static uint8_t ow_crc8(const uint8_t *data, size_t len) {
	uint8_t crc = 0;
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (uint8_t bit = 0; bit < 8; bit++) {
			if (crc & 0x01) {
				crc = (crc >> 1) ^ 0x8c;
			} else {
				crc = crc >> 1;
			}
		}
	}
	return crc;
}


/* Discard any stale bytes left in the UART receive buffer before starting a new transfer. */
static void ow_flush(Tmp1826 *self) {
	uint8_t tmp[16];
	size_t read = 0;
	while (self->stream->vmt->read_timeout(self->stream, tmp, sizeof(tmp), &read, 0) == STREAM_RET_OK) {
		;
	}
}


/* Read exactly @p len bytes from the stream, looping until they all arrive or a timeout occurs. */
static tmp1826_ret_t ow_read_exact(Tmp1826 *self, uint8_t *buf, size_t len) {
	size_t got = 0;
	while (got < len) {
		size_t read = 0;
		if (self->stream->vmt->read_timeout(self->stream, buf + got, len - got, &read,
		                                    TMP1826_IO_TIMEOUT_MS) != STREAM_RET_OK) {
			return TMP1826_RET_FAILED;
		}
		got += read;
	}
	return TMP1826_RET_OK;
}


/*
 * Transfer @p n 1-Wire bytes in a single UART burst. Each 1-Wire bit is encoded as one UART byte: 0xFF drives a
 * short low pulse (write/read '1') and 0x00 drives a long low pulse (write '0'). On a single-wire half-duplex UART
 * the line state is read back for every transmitted byte, so the device response is recovered by sampling the echo:
 * a returned 0xFF means the line stayed high (bit '1'), anything else means the device pulled it low (bit '0').
 *
 * If @p out is NULL all bit slots are read slots (0xFF). If @p in is NULL the read-back bytes are discarded.
 */
static tmp1826_ret_t ow_io(Tmp1826 *self, const uint8_t *out, uint8_t *in, size_t n) {
	if (n == 0 || n > TMP1826_OW_MAX_BYTES) {
		return TMP1826_RET_FAILED;
	}

	uint8_t tx[TMP1826_OW_MAX_BYTES * 8];
	uint8_t rx[TMP1826_OW_MAX_BYTES * 8];

	for (size_t i = 0; i < n; i++) {
		uint8_t b = (out != NULL) ? out[i] : 0xff;
		for (size_t bit = 0; bit < 8; bit++) {
			tx[i * 8 + bit] = (b & (1 << bit)) ? 0xff : 0x00;
		}
	}

	ow_flush(self);
	if (self->stream->vmt->write(self->stream, tx, n * 8) != STREAM_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	if (ow_read_exact(self, rx, n * 8) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}

	if (in != NULL) {
		for (size_t i = 0; i < n; i++) {
			uint8_t b = 0;
			for (size_t bit = 0; bit < 8; bit++) {
				if (rx[i * 8 + bit] == 0xff) {
					b |= (1 << bit);
				}
			}
			in[i] = b;
		}
	}
	return TMP1826_RET_OK;
}


/* Generate a 1-Wire bus reset and detect the device presence pulse. */
static tmp1826_ret_t ow_reset(Tmp1826 *self, bool *present) {
	self->uart->vmt->set_bitrate(self->uart, TMP1826_OW_RESET_BAUD);
	ow_flush(self);

	uint8_t tx = 0xf0;
	uint8_t rx = 0xf0;
	tmp1826_ret_t ret = TMP1826_RET_OK;
	if (self->stream->vmt->write(self->stream, &tx, 1) != STREAM_RET_OK ||
	    ow_read_exact(self, &rx, 1) != TMP1826_RET_OK) {
		ret = TMP1826_RET_FAILED;
	}

	self->uart->vmt->set_bitrate(self->uart, TMP1826_OW_DATA_BAUD);
	if (ret != TMP1826_RET_OK) {
		return ret;
	}

	/* Without a device the line follows our transmission and reads back as 0xF0; a present device pulls the line
	 * low during the presence window, corrupting the echoed byte. */
	if (present != NULL) {
		*present = (rx != 0xf0);
	}
	return TMP1826_RET_OK;
}


/* Reset the bus and address the (single) device on it using SKIP ROM. */
static tmp1826_ret_t tmp1826_select(Tmp1826 *self) {
	bool present = false;
	if (ow_reset(self, &present) != TMP1826_RET_OK || !present) {
		return TMP1826_RET_FAILED;
	}
	uint8_t cmd = TMP1826_ROM_SKIP;
	return ow_io(self, &cmd, NULL, 1);
}


/**********************************************************************************************************************
 * TMP1826 device access
 **********************************************************************************************************************/

/* Read the 64-bit ROM unique address of the single device on the bus (used as a presence probe at init). */
static tmp1826_ret_t tmp1826_read_rom(Tmp1826 *self, uint8_t *rom) {
	bool present = false;
	if (ow_reset(self, &present) != TMP1826_RET_OK || !present) {
		return TMP1826_RET_FAILED;
	}
	uint8_t cmd = TMP1826_ROM_READ;
	if (ow_io(self, &cmd, NULL, 1) != TMP1826_RET_OK ||
	    ow_io(self, NULL, rom, 8) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	if (ow_crc8(rom, 7) != rom[7]) {
		return TMP1826_RET_FAILED;
	}
	return TMP1826_RET_OK;
}


/* Trigger a one-shot conversion and read back the resulting die temperature in degrees Celsius. */
static tmp1826_ret_t tmp1826_measure(Tmp1826 *self, float *value) {
	if (tmp1826_select(self) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t cmd = TMP1826_FUNC_CONVERT_TEMP;
	if (ow_io(self, &cmd, NULL, 1) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}

	/* The bus must stay idle while the bus-powered device performs the conversion. */
	vTaskDelay(pdMS_TO_TICKS(TMP1826_CONV_TIME_MS));

	if (tmp1826_select(self) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	cmd = TMP1826_FUNC_READ_SCRATCHPAD1;
	if (ow_io(self, &cmd, NULL, 1) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}

	/* The device sends the first 8 scratchpad-1 bytes followed by their CRC. */
	uint8_t sp[9] = {0};
	if (ow_io(self, NULL, sp, sizeof(sp)) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	if (ow_crc8(sp, 8) != sp[8]) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("scratchpad CRC mismatch"));
		return TMP1826_RET_FAILED;
	}

	/* Bytes 0/1 hold the temperature result (LSB first), byte 4 the device configuration-1 register. The result is
	 * two's complement; its resolution depends on the selected format (7.8125 m vs 62.5 m degrees Celsius). */
	int16_t raw = (int16_t)((uint16_t)sp[1] << 8 | sp[0]);
	if (sp[4] & TMP1826_CONFIG1_TEMP_FMT) {
		*value = (float)raw / 128.0f;
	} else {
		*value = (float)raw / 16.0f;
	}
	return TMP1826_RET_OK;
}


/* Read one 8-byte EEPROM block at the block-aligned address @p addr. */
static tmp1826_ret_t tmp1826_eeprom_read_block(Tmp1826 *self, uint16_t addr, uint8_t *data) {
	if (tmp1826_select(self) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t cmd[3] = {TMP1826_FUNC_READ_EEPROM, (addr >> 8) & 0xff, addr & 0xff};
	if (ow_io(self, cmd, NULL, sizeof(cmd)) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	/* The device needs an idle gap after the address before it starts streaming the block data. */
	vTaskDelay(pdMS_TO_TICKS(TMP1826_READ_IDLE_MS));
	return ow_io(self, NULL, data, TMP1826_EEPROM_BLOCK);
}


/* Write one full 8-byte EEPROM block at the block-aligned address @p addr and commit it to the EEPROM. */
static tmp1826_ret_t tmp1826_eeprom_write_block(Tmp1826 *self, uint16_t addr, const uint8_t *data) {
	/* Stage the address and data in scratchpad-2; the device echoes a CRC over the 2 address and 8 data bytes. */
	if (tmp1826_select(self) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t cmd[3] = {TMP1826_FUNC_WRITE_SCRATCHPAD2, (addr >> 8) & 0xff, addr & 0xff};
	uint8_t crc = 0;
	if (ow_io(self, cmd, NULL, sizeof(cmd)) != TMP1826_RET_OK ||
	    ow_io(self, data, NULL, TMP1826_EEPROM_BLOCK) != TMP1826_RET_OK ||
	    ow_io(self, NULL, &crc, 1) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t check[2 + TMP1826_EEPROM_BLOCK] = {cmd[1], cmd[2]};
	memcpy(check + 2, data, TMP1826_EEPROM_BLOCK);
	if (ow_crc8(check, sizeof(check)) != crc) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("scratchpad-2 CRC mismatch"));
		return TMP1826_RET_FAILED;
	}

	/* Commit the staged scratchpad-2 content to the user EEPROM and wait for the programming to finish. */
	if (tmp1826_select(self) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t copy[2] = {TMP1826_FUNC_COPY_SCRATCHPAD2, TMP1826_COPY_COMMIT};
	if (ow_io(self, copy, NULL, sizeof(copy)) != TMP1826_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	vTaskDelay(pdMS_TO_TICKS(TMP1826_PROG_TIME_MS));
	return TMP1826_RET_OK;
}


/**********************************************************************************************************************
 * Sensor interface API
 **********************************************************************************************************************/

static sensor_ret_t temp_sensor_value_f(Sensor *sensor, float *value) {
	Tmp1826 *self = sensor->parent;
	if (value == NULL) {
		return SENSOR_RET_FAILED;
	}

	sensor_ret_t ret = SENSOR_RET_FAILED;
	xSemaphoreTake(self->lock, portMAX_DELAY);
	if (tmp1826_measure(self, value) == TMP1826_RET_OK) {
		ret = SENSOR_RET_OK;
	}
	xSemaphoreGive(self->lock);

	return ret;
}


static const struct sensor_vmt temp_sensor_vmt = {
	.value_f = temp_sensor_value_f,
};


static const struct sensor_info temp_sensor_info = {
	.description = "TMP1826 die temperature",
	.unit = "°C",
};


/**********************************************************************************************************************
 * Flash interface API (on-chip user EEPROM)
 **********************************************************************************************************************/

static flash_ret_t eeprom_get_size(Flash *flash, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	(void)flash;
	if (size == NULL || ops == NULL) {
		return FLASH_RET_BAD_ARG;
	}
	switch (i) {
		case 0:
			*size = TMP1826_EEPROM_SIZE;
			*ops = FLASH_BLOCK_OPS_READ | FLASH_BLOCK_OPS_WRITE | FLASH_BLOCK_OPS_ERASE;
			return FLASH_RET_OK;
		case 1:
			*size = TMP1826_EEPROM_BLOCK;
			*ops = FLASH_BLOCK_OPS_READ | FLASH_BLOCK_OPS_WRITE | FLASH_BLOCK_OPS_ERASE;
			return FLASH_RET_OK;
		default:
			return FLASH_RET_BAD_ARG;
	}
}


static flash_ret_t eeprom_read(Flash *flash, const size_t addr, void *buf, size_t len) {
	Tmp1826 *self = flash->parent;
	if (buf == NULL) {
		return FLASH_RET_BAD_ARG;
	}
	if (addr + len > TMP1826_EEPROM_SIZE) {
		return FLASH_RET_BAD_ARG;
	}

	flash_ret_t ret = FLASH_RET_OK;
	xSemaphoreTake(self->lock, portMAX_DELAY);

	uint8_t *out = buf;
	size_t done = 0;
	/* The EEPROM is only accessible in 8-byte blocks, so read the covering blocks and copy out the slice. */
	while (done < len) {
		uint16_t block = (addr + done) & ~(TMP1826_EEPROM_BLOCK - 1);
		size_t off = (addr + done) - block;
		size_t chunk = TMP1826_EEPROM_BLOCK - off;
		if (chunk > len - done) {
			chunk = len - done;
		}
		uint8_t blk[TMP1826_EEPROM_BLOCK];
		if (tmp1826_eeprom_read_block(self, block, blk) != TMP1826_RET_OK) {
			ret = FLASH_RET_FAILED;
			break;
		}
		memcpy(out + done, blk + off, chunk);
		done += chunk;
	}

	xSemaphoreGive(self->lock);
	return ret;
}


static flash_ret_t eeprom_write(Flash *flash, const size_t addr, const void *buf, size_t len) {
	Tmp1826 *self = flash->parent;
	if (buf == NULL) {
		return FLASH_RET_BAD_ARG;
	}
	if (addr + len > TMP1826_EEPROM_SIZE) {
		return FLASH_RET_BAD_ARG;
	}

	flash_ret_t ret = FLASH_RET_OK;
	xSemaphoreTake(self->lock, portMAX_DELAY);

	const uint8_t *in = buf;
	size_t done = 0;
	/* The device only programs whole 8-byte blocks, so read-modify-write each touched block. */
	while (done < len) {
		uint16_t block = (addr + done) & ~(TMP1826_EEPROM_BLOCK - 1);
		size_t off = (addr + done) - block;
		size_t chunk = TMP1826_EEPROM_BLOCK - off;
		if (chunk > len - done) {
			chunk = len - done;
		}
		uint8_t blk[TMP1826_EEPROM_BLOCK];
		if (chunk != TMP1826_EEPROM_BLOCK &&
		    tmp1826_eeprom_read_block(self, block, blk) != TMP1826_RET_OK) {
			ret = FLASH_RET_FAILED;
			break;
		}
		memcpy(blk + off, in + done, chunk);
		if (tmp1826_eeprom_write_block(self, block, blk) != TMP1826_RET_OK) {
			ret = FLASH_RET_FAILED;
			break;
		}
		done += chunk;
	}

	xSemaphoreGive(self->lock);
	return ret;
}


static flash_ret_t eeprom_erase(Flash *flash, const size_t addr, size_t len) {
	Tmp1826 *self = flash->parent;
	/* Erase is only defined on whole-block boundaries. */
	if ((addr % TMP1826_EEPROM_BLOCK) != 0 || (len % TMP1826_EEPROM_BLOCK) != 0) {
		return FLASH_RET_BAD_ARG;
	}
	if (addr + len > TMP1826_EEPROM_SIZE) {
		return FLASH_RET_BAD_ARG;
	}

	flash_ret_t ret = FLASH_RET_OK;
	xSemaphoreTake(self->lock, portMAX_DELAY);

	uint8_t blk[TMP1826_EEPROM_BLOCK];
	memset(blk, 0xff, sizeof(blk));
	for (size_t off = 0; off < len; off += TMP1826_EEPROM_BLOCK) {
		if (tmp1826_eeprom_write_block(self, addr + off, blk) != TMP1826_RET_OK) {
			ret = FLASH_RET_FAILED;
			break;
		}
	}

	xSemaphoreGive(self->lock);
	return ret;
}


static const struct flash_vmt eeprom_flash_vmt = {
	.get_size = eeprom_get_size,
	.erase = eeprom_erase,
	.write = eeprom_write,
	.read = eeprom_read,
};


/**********************************************************************************************************************
 * Public API
 **********************************************************************************************************************/

tmp1826_ret_t tmp1826_init(Tmp1826 *self, Uart *uart, Stream *stream) {
	if (u_assert(self != NULL) ||
	    u_assert(uart != NULL) ||
	    u_assert(stream != NULL)) {
		return TMP1826_RET_FAILED;
	}
	memset(self, 0, sizeof(Tmp1826));
	self->uart = uart;
	self->stream = stream;

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return TMP1826_RET_FAILED;
	}

	self->flash.parent = self;
	self->flash.vmt = &eeprom_flash_vmt;

	self->temp.parent = self;
	self->temp.vmt = &temp_sensor_vmt;
	self->temp.info = &temp_sensor_info;

	/* Probe the bus by reading the device 64-bit ROM address. */
	uint8_t rom[8] = {0};
	xSemaphoreTake(self->lock, portMAX_DELAY);
	tmp1826_ret_t ret = tmp1826_read_rom(self, rom);
	xSemaphoreGive(self->lock);
	if (ret != TMP1826_RET_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("TMP1826 not detected on the 1-Wire bus"));
		tmp1826_free(self);
		return TMP1826_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("initialized, rom = %02x%02x%02x%02x%02x%02x%02x%02x"),
		rom[7], rom[6], rom[5], rom[4], rom[3], rom[2], rom[1], rom[0]);
	return TMP1826_RET_OK;
}


tmp1826_ret_t tmp1826_free(Tmp1826 *self) {
	if (u_assert(self != NULL)) {
		return TMP1826_RET_FAILED;
	}
	if (self->lock != NULL) {
		vSemaphoreDelete(self->lock);
		self->lock = NULL;
	}
	return TMP1826_RET_OK;
}


tmp1826_ret_t tmp1826_get_flash(Tmp1826 *self, Flash **flash) {
	if (u_assert(self != NULL) ||
	    u_assert(flash != NULL)) {
		return TMP1826_RET_FAILED;
	}
	*flash = &self->flash;
	return TMP1826_RET_OK;
}


tmp1826_ret_t tmp1826_get_sensor(Tmp1826 *self, Sensor **sensor) {
	if (u_assert(self != NULL) ||
	    u_assert(sensor != NULL)) {
		return TMP1826_RET_FAILED;
	}
	*sensor = &self->temp;
	return TMP1826_RET_OK;
}
