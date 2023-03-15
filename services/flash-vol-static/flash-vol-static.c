/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Flash volumes service (static configuration)
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "u_log.h"
#include "u_assert.h"

#include <interfaces/flash.h>

#include "flash-vol-static.h"

#define MODULE_NAME "flash-vol-static"


static flash_ret_t lv_get_size(Flash *self, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	struct flash_vol_lv *lv = (struct flash_vol_lv *)self;
	if (i == 0) {
		/* Full chip size, allow erase. */
		*size = lv->size;
		*ops = FLASH_BLOCK_OPS_ERASE;
		return FLASH_RET_OK;
	}
	return lv->parent->pv->vmt->get_size(lv->parent->pv, i, size, ops);
}


static flash_ret_t lv_erase(Flash *self, const size_t addr, size_t len) {
	struct flash_vol_lv *lv = (struct flash_vol_lv *)self;
	if (addr >= lv->size || (addr + len) > lv->size) {
		/* Out of bounds of the current LV */
		return FLASH_RET_FAILED;
	}
	if (addr == 0 && len == lv->size) {
		/* Full chip erase. Implement with block erases. */

		size_t block_size = 0;
		flash_block_ops_t ops = 0;
		if (lv->parent->pv->vmt->get_size(lv->parent->pv, 1, &block_size, &ops) != FLASH_RET_OK) {
			return FLASH_RET_FAILED;
		}

		for (size_t i = 0; i < (len / block_size); i++) {
			if (lv->parent->pv->vmt->erase(lv->parent->pv, lv->start + i * block_size, block_size) != FLASH_RET_OK) {
				return FLASH_RET_FAILED;
			}
		}

		return FLASH_RET_OK;
	}
	return lv->parent->pv->vmt->erase(lv->parent->pv, addr + lv->start, len);
}


static flash_ret_t lv_write(Flash *self, const size_t addr, const void *buf, size_t len) {
	struct flash_vol_lv *lv = (struct flash_vol_lv *)self;
	if (addr >= lv->size || (addr + len) > lv->size) {
		/* Out of bounds of the current LV */
		return FLASH_RET_FAILED;
	}
	return lv->parent->pv->vmt->write(lv->parent->pv, addr + lv->start, buf, len);
}


static flash_ret_t lv_read(Flash *self, const size_t addr, void *buf, size_t len) {
	struct flash_vol_lv *lv = (struct flash_vol_lv *)self;
	if (addr >= lv->size || (addr + len) > lv->size) {
		/* Out of bounds of the current LV */
		return FLASH_RET_FAILED;
	}
	return lv->parent->pv->vmt->read(lv->parent->pv, addr + lv->start, buf, len);
}


static const struct flash_vmt lv_vmt = {
	.get_size = lv_get_size,
	.erase = lv_erase,
	.write = lv_write,
	.read = lv_read,
};


flash_vol_static_ret_t flash_vol_static_init(FlashVolStatic *self, Flash *pv) {
	if (u_assert(self != NULL) ||
	    u_assert(pv != NULL)) {
		return FLASH_VOL_STATIC_RET_FAILED;
	}
	memset(self, 0, sizeof(FlashVolStatic));
	self->pv = pv;

	return FLASH_VOL_STATIC_RET_OK;
}


flash_vol_static_ret_t flash_vol_static_free(FlashVolStatic *self) {
	if (u_assert(self != NULL)) {
		return FLASH_VOL_STATIC_RET_FAILED;
	}

	/* Nothing to free */

	return FLASH_VOL_STATIC_RET_OK;
}


flash_vol_static_ret_t flash_vol_static_create(FlashVolStatic *self, const char *name, size_t start, size_t size, Flash **lv) {
	if (u_assert(self != NULL) ||
	    u_assert(name != NULL) ||
	    u_assert(lv != NULL)) {
		return FLASH_VOL_STATIC_RET_FAILED;
	}

	for (size_t i = 0; i < FLASH_VOL_STATIC_LVS_MAX; i++) {
		if (self->lvs[i].parent == NULL) {
			u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("creating LV '%s', start 0x%x, size %lu K"),
				name,
				start,
				size / 1024
			);

			self->lvs[i].name = name;
			self->lvs[i].start = start;
			self->lvs[i].size = size;
			self->lvs[i].lv.vmt = &lv_vmt;
			self->lvs[i].lv.parent = self;
			self->lvs[i].parent = self;
			*lv = &self->lvs[i].lv;
			return FLASH_VOL_STATIC_RET_OK;
		}
	}
	return FLASH_VOL_STATIC_RET_FAILED;
}




