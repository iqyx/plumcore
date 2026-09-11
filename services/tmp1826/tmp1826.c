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
#include <stdio.h>
#include <string.h>

#include <main.h>

#include <interfaces/ow.h>
#include <interfaces/flash.h>
#include <interfaces/sensor.h>

#include "tmp1826.h"

#define MODULE_NAME "tmp1826"


/* Worst case active conversion time is ~6 ms (CONV_TIME_SEL = 5.5 ms); keep a margin. */
#define TMP1826_CONV_TIME_MS 12

/* EEPROM 8-byte word programming time, tPROG max = 21 ms; keep a margin. */
#define TMP1826_PROG_TIME_MS 25

/* Idle bus time the device needs after the read address before it streams a block, tREADIDLE = 560 us. At a 1 kHz
 * tick a vTaskDelay of 1 rounds down to as little as ~0 us (tick quantization) and can undershoot tREADIDLE, which
 * corrupts the first read bit-slots; use 2 ticks to guarantee at least ~1 ms of idle. */
#define TMP1826_READ_IDLE_MS 2

/* Number of additional attempts made when an EEPROM block read or write fails (0 = try once, no retry). */
#define TMP1826_EEPROM_RETRIES CONFIG_SERVICE_TMP1826_EEPROM_RETRIES

/* ROM commands. */
#define TMP1826_ROM_READ 0x33

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
 * TMP1826 device access
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


/* Read the 64-bit ROM unique address of the single device on the bus (used as a presence probe at init). */
static tmp1826_ret_t tmp1826_read_rom(Ow *ow, uint8_t *rom) {
	bool present = false;
	if (ow->vmt->reset(ow, &present) != OW_RET_OK || !present) {
		return TMP1826_RET_FAILED;
	}
	uint8_t cmd = TMP1826_ROM_READ;
	if (ow->vmt->exchange(ow, &cmd, NULL, 1) != OW_RET_OK ||
	    ow->vmt->exchange(ow, NULL, rom, 8) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	if (ow_crc8(rom, 7) != rom[7]) {
		return TMP1826_RET_FAILED;
	}
	return TMP1826_RET_OK;
}


/* Trigger a one-shot conversion and read back the resulting die temperature in degrees Celsius. */
static tmp1826_ret_t tmp1826_measure(Tmp1826 *self, float *value) {
	if (self->ow->vmt->select(self->ow) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t cmd = TMP1826_FUNC_CONVERT_TEMP;
	if (self->ow->vmt->exchange(self->ow, &cmd, NULL, 1) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}

	/* The bus must stay idle while the bus-powered device performs the conversion. */
	vTaskDelay(pdMS_TO_TICKS(TMP1826_CONV_TIME_MS));

	if (self->ow->vmt->select(self->ow) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	cmd = TMP1826_FUNC_READ_SCRATCHPAD1;
	if (self->ow->vmt->exchange(self->ow, &cmd, NULL, 1) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}

	/* The device sends the first 8 scratchpad-1 bytes followed by their CRC. */
	uint8_t sp[9] = {0};
	if (self->ow->vmt->exchange(self->ow, NULL, sp, sizeof(sp)) != OW_RET_OK) {
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


/* Read one 8-byte EEPROM block at the block-aligned address @p addr (single READ EEPROM transaction).
 *
 * Per the datasheet (READ EEPROM, F0h) the device provides no CRC in the response, so a single read cannot be
 * integrity-checked on its own; the redundant-read comparison in tmp1826_eeprom_read_block() provides that. */
static tmp1826_ret_t tmp1826_eeprom_read_block_once(Tmp1826 *self, uint16_t addr, uint8_t *data) {
	if (self->ow->vmt->select(self->ow) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t cmd[3] = {TMP1826_FUNC_READ_EEPROM, (addr >> 8) & 0xff, addr & 0xff};
	if (self->ow->vmt->exchange(self->ow, cmd, NULL, sizeof(cmd)) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	/* The device needs an idle gap (tREADIDLE) after the address before it starts streaming the block data. */
	vTaskDelay(pdMS_TO_TICKS(TMP1826_READ_IDLE_MS));
	if (self->ow->vmt->exchange(self->ow, NULL, data, TMP1826_EEPROM_BLOCK) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	return TMP1826_RET_OK;
}


/* Write one full 8-byte EEPROM block at @p addr, commit it, and read it back to confirm (single attempt, no retry). */
static tmp1826_ret_t tmp1826_eeprom_write_block_once(Tmp1826 *self, uint16_t addr, const uint8_t *data) {
	/* Stage the address and data in scratchpad-2; the device echoes a CRC over the 2 address and 8 data bytes. */
	if (self->ow->vmt->select(self->ow) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t cmd[3] = {TMP1826_FUNC_WRITE_SCRATCHPAD2, (addr >> 8) & 0xff, addr & 0xff};
	uint8_t crc = 0;
	if (self->ow->vmt->exchange(self->ow, cmd, NULL, sizeof(cmd)) != OW_RET_OK ||
	    self->ow->vmt->exchange(self->ow, data, NULL, TMP1826_EEPROM_BLOCK) != OW_RET_OK ||
	    self->ow->vmt->exchange(self->ow, NULL, &crc, 1) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t check[2 + TMP1826_EEPROM_BLOCK] = {cmd[1], cmd[2]};
	memcpy(check + 2, data, TMP1826_EEPROM_BLOCK);
	if (ow_crc8(check, sizeof(check)) != crc) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("scratchpad-2 CRC mismatch"));
		return TMP1826_RET_FAILED;
	}

	/* Commit the staged scratchpad-2 content to the user EEPROM and wait for the programming to finish. */
	if (self->ow->vmt->select(self->ow) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	uint8_t copy[2] = {TMP1826_FUNC_COPY_SCRATCHPAD2, TMP1826_COPY_COMMIT};
	if (self->ow->vmt->exchange(self->ow, copy, NULL, sizeof(copy)) != OW_RET_OK) {
		return TMP1826_RET_FAILED;
	}
	vTaskDelay(pdMS_TO_TICKS(TMP1826_PROG_TIME_MS));

	/* Confirm the commit actually landed by reading the block back and comparing it against what we wrote. This is a
	 * CRC-independent end-to-end check of the whole stage-commit-read path. */
	uint8_t readback[TMP1826_EEPROM_BLOCK] = {0};
	if (tmp1826_eeprom_read_block_once(self, addr, readback) != TMP1826_RET_OK ||
	    memcmp(readback, data, TMP1826_EEPROM_BLOCK) != 0) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("EEPROM write verify mismatch at block 0x%04x"), addr);
		return TMP1826_RET_FAILED;
	}
	return TMP1826_RET_OK;
}


/* Read one 8-byte EEPROM block with integrity checking.
 *
 * READ EEPROM (F0h) carries no CRC, so the only device-level defense against a transient bus error on a read is to
 * read the block twice and require two identical results. On a mismatch (or a failed transaction) the pair is retried
 * up to TMP1826_EEPROM_RETRIES additional times. This catches transient/random corruption; a stuck bit that repeats
 * identically across reads cannot be detected without a stored application-level checksum. */
static tmp1826_ret_t tmp1826_eeprom_read_block(Tmp1826 *self, uint16_t addr, uint8_t *data) {
	for (unsigned int attempt = 0; attempt <= TMP1826_EEPROM_RETRIES; attempt++) {
		uint8_t first[TMP1826_EEPROM_BLOCK];
		uint8_t second[TMP1826_EEPROM_BLOCK];
		tmp1826_ret_t r1 = tmp1826_eeprom_read_block_once(self, addr, first);
		tmp1826_ret_t r2 = tmp1826_eeprom_read_block_once(self, addr, second);
		if (r1 == TMP1826_RET_OK && r2 == TMP1826_RET_OK && memcmp(first, second, TMP1826_EEPROM_BLOCK) == 0) {
			memcpy(data, first, TMP1826_EEPROM_BLOCK);
			return TMP1826_RET_OK;
		}
		if (attempt >= TMP1826_EEPROM_RETRIES) {
			break;
		}
		if (r1 == TMP1826_RET_OK && r2 == TMP1826_RET_OK) {
			/* Both reads succeeded but disagree: log the per-byte XOR of the two so the pattern of the corrupted
			 * bits is visible (a set bit marks a byte position that differed between the two reads). */
			char diff[3 * TMP1826_EEPROM_BLOCK + 1] = {0};
			for (size_t i = 0; i < TMP1826_EEPROM_BLOCK; i++) {
				snprintf(diff + 3 * i, 4, "%02x ", (uint8_t)(first[i] ^ second[i]));
			}
			u_log(system_log, LOG_TYPE_WARN,
				U_LOG_MODULE_PREFIX("EEPROM read of block 0x%04x inconsistent, retry %u/%u, xor=%s"),
				addr, attempt + 1, TMP1826_EEPROM_RETRIES, diff);
		} else {
			u_log(system_log, LOG_TYPE_WARN,
				U_LOG_MODULE_PREFIX("EEPROM read of block 0x%04x transaction failed, retry %u/%u"),
				addr, attempt + 1, TMP1826_EEPROM_RETRIES);
		}
	}
	return TMP1826_RET_FAILED;
}


/* Write one 8-byte EEPROM block, retrying up to TMP1826_EEPROM_RETRIES additional times on any failure. */
static tmp1826_ret_t tmp1826_eeprom_write_block(Tmp1826 *self, uint16_t addr, const uint8_t *data) {
	tmp1826_ret_t ret = TMP1826_RET_FAILED;
	for (unsigned int attempt = 0; attempt <= TMP1826_EEPROM_RETRIES; attempt++) {
		ret = tmp1826_eeprom_write_block_once(self, addr, data);
		if (ret == TMP1826_RET_OK) {
			break;
		}
		if (attempt < TMP1826_EEPROM_RETRIES) {
			u_log(system_log, LOG_TYPE_WARN,
				U_LOG_MODULE_PREFIX("EEPROM write of block 0x%04x failed, retry %u/%u"),
				addr, attempt + 1, TMP1826_EEPROM_RETRIES);
		}
	}
	return ret;
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

tmp1826_ret_t tmp1826_probe(Ow *ow, uint8_t id[TMP1826_ID_SIZE]) {
	if (u_assert(ow != NULL) ||
	    u_assert(id != NULL)) {
		return TMP1826_RET_FAILED;
	}
	/* The ROM read only resolves when a single device drives the bus; a populated, CRC-valid address is proof of a
	 * detected device and serves as its unique id. */
	return tmp1826_read_rom(ow, id);
}


tmp1826_ret_t tmp1826_init(Tmp1826 *self, Ow *ow) {
	if (u_assert(self != NULL) ||
	    u_assert(ow != NULL)) {
		return TMP1826_RET_FAILED;
	}
	memset(self, 0, sizeof(Tmp1826));
	self->ow = ow;

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return TMP1826_RET_FAILED;
	}

	self->flash.parent = self;
	self->flash.vmt = &eeprom_flash_vmt;

	self->temp.parent = self;
	self->temp.vmt = &temp_sensor_vmt;
	self->temp.info = &temp_sensor_info;

	/* Probe the bus for a device by reading its 64-bit ROM address. Nothing else can reach this freshly created
	 * instance yet, so the probe runs without taking the bus lock. */
	uint8_t rom[TMP1826_ID_SIZE] = {0};
	if (tmp1826_probe(self->ow, rom) != TMP1826_RET_OK) {
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
