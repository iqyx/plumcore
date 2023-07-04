/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Driver for EEPROM memories connected over the I2C bus
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <main.h>
#include <interfaces/i2c-bus.h>
#include <interfaces/flash.h>
#include "i2c-eeprom.h"

#define MODULE_NAME "i2c-eeprom"


/***************************************************************************************************
 * Flash interface implementation
 ***************************************************************************************************/

/* There is basically no way to determine size of the blocks with I2C EEPROMS.
 * We are returning values passed to the init function. */
static flash_ret_t i2c_eeprom_get_size(Flash *flash, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	I2cEeprom *self = (I2cEeprom *)flash->parent;

	switch (i) {
		case 0:
			*size = self->eeprom_size;
			/* No mass erase operation possible */
			*ops = 0;
			break;
		case 1:
			/* Maximum page size is usually 32 bytes for EEPROM memories. */
			*size = self->page_size;
			*ops = FLASH_BLOCK_OPS_READ + FLASH_BLOCK_OPS_WRITE + FLASH_BLOCK_OPS_ERASE;
			break;
		default:
			return FLASH_RET_BAD_ARG;
	}
	return FLASH_RET_OK;
}


static flash_ret_t i2c_eeprom_erase(Flash *flash, const size_t addr, size_t len) {
	I2cEeprom *self = (I2cEeprom *)flash->parent;

	return FLASH_RET_OK;
}


static flash_ret_t i2c_eeprom_write(Flash *flash, const size_t addr, const void *buf, size_t len) {
	I2cEeprom *self = (I2cEeprom *)flash->parent;

	/** @todo address checks */

	uint8_t *txdata = alloca(len + 2);
	txdata[0] = addr >> 8;
	txdata[1] = addr & 0xff;
	memcpy(txdata + 2, buf, len);

	if (self->i2c->transfer(self->i2c->parent, self->addr, txdata, len + 2, NULL, 0) == I2C_BUS_RET_OK) {
		/* Datasheet value for M24C32, maximum 10 ms */
		vTaskDelay(10);
		return FLASH_RET_OK;
	}

	return FLASH_RET_OK;
}


static flash_ret_t i2c_eeprom_read(Flash *flash, const size_t addr, void *buf, size_t len) {
	I2cEeprom *self = (I2cEeprom *)flash->parent;

	uint8_t txdata[2] = {addr >> 8, addr & 0xff};

	if (self->i2c->transfer(self->i2c->parent, self->addr, txdata, sizeof(txdata), buf, len) == I2C_BUS_RET_OK) {
		return FLASH_RET_OK;
	}

	return FLASH_RET_FAILED;
}


static const struct flash_vmt i2c_eeprom_vmt = {
	.get_size = i2c_eeprom_get_size,
	.erase = i2c_eeprom_erase,
	.write = i2c_eeprom_write,
	.read = i2c_eeprom_read,
};


i2c_eeprom_ret_t i2c_eeprom_init(I2cEeprom *self, I2cBus *i2c, uint8_t addr, size_t eeprom_size, size_t page_size) {
	if (u_assert(self != NULL) ||
	    u_assert(i2c != NULL)) {
		return I2C_EEPROM_RET_NULL;
	}

	memset(self, 0, sizeof(I2cEeprom));
	self->i2c = i2c;
	self->addr = addr;
	self->eeprom_size = eeprom_size;
	self->page_size = page_size;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("device address 0x%02x, memory size %lu B, page size %lu B"), addr, eeprom_size, page_size);

	self->flash.parent = self;
	self->flash.vmt = &i2c_eeprom_vmt;

	return I2C_EEPROM_RET_OK;
}


i2c_eeprom_ret_t i2c_eeprom_free(I2cEeprom *self) {
	if (u_assert(self != NULL)) {
		return I2C_EEPROM_RET_NULL;
	}

	return I2C_EEPROM_RET_OK;
}



