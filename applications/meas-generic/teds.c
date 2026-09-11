/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * TEDS (Transducer Electronic Data Sheet) discovery over a 1-Wire bus
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <main.h>

#include <interfaces/servicelocator.h>
#include <interfaces/ow.h>
#include <interfaces/mux.h>
#include <interfaces/flash.h>

#include <services/tmp1826/tmp1826.h>

#include "teds.h"

#define MODULE_NAME "app-meas-generic-teds"

#define TEDS_SETTLE_MS 10


/* Steer the mux to @p channel, settling the freshly powered bus only when the selection actually changes. The caller
 * must hold the TEDS lock. */
static void teds_mux_select(Teds *self, uint32_t channel) {
	if (self->current_channel == channel) {
		return;
	}
	self->mux->vmt->select(self->mux, channel);
	self->current_channel = channel;
	vTaskDelay(pdMS_TO_TICKS(TEDS_SETTLE_MS));
}


/**********************************************************************************************************************
 * Flash interface proxy
 *
 * The discovered EEPROM drivers expose their own Flash interface, but it can only be used while the mux points at the
 * driver's channel. Each channel therefore exports this proxy: it locks the shared bus, steers the mux to the channel
 * the called Flash instance belongs to and delegates the operation to the underlying driver.
 **********************************************************************************************************************/

static flash_ret_t teds_flash_get_size(Flash *self, uint32_t i, size_t *size, flash_block_ops_t *ops) {
	struct teds_channel *ch = self->parent;
	if (ch->target == NULL) {
		return FLASH_RET_FAILED;
	}

	xSemaphoreTake(ch->parent->lock, portMAX_DELAY);
	teds_mux_select(ch->parent, ch->index);
	flash_ret_t ret = ch->target->vmt->get_size(ch->target, i, size, ops);
	xSemaphoreGive(ch->parent->lock);

	return ret;
}


static flash_ret_t teds_flash_erase(Flash *self, const size_t addr, size_t len) {
	struct teds_channel *ch = self->parent;
	if (ch->target == NULL) {
		return FLASH_RET_FAILED;
	}

	xSemaphoreTake(ch->parent->lock, portMAX_DELAY);
	teds_mux_select(ch->parent, ch->index);
	flash_ret_t ret = ch->target->vmt->erase(ch->target, addr, len);
	xSemaphoreGive(ch->parent->lock);

	return ret;
}


static flash_ret_t teds_flash_write(Flash *self, const size_t addr, const void *buf, size_t len) {
	struct teds_channel *ch = self->parent;
	if (ch->target == NULL) {
		return FLASH_RET_FAILED;
	}

	xSemaphoreTake(ch->parent->lock, portMAX_DELAY);
	teds_mux_select(ch->parent, ch->index);
	flash_ret_t ret = ch->target->vmt->write(ch->target, addr, buf, len);
	xSemaphoreGive(ch->parent->lock);

	return ret;
}


static flash_ret_t teds_flash_read(Flash *self, const size_t addr, void *buf, size_t len) {
	struct teds_channel *ch = self->parent;
	if (ch->target == NULL) {
		return FLASH_RET_FAILED;
	}

	xSemaphoreTake(ch->parent->lock, portMAX_DELAY);
	teds_mux_select(ch->parent, ch->index);
	flash_ret_t ret = ch->target->vmt->read(ch->target, addr, buf, len);
	xSemaphoreGive(ch->parent->lock);

	return ret;
}


static const struct flash_vmt teds_flash_vmt = {
	.get_size = teds_flash_get_size,
	.erase = teds_flash_erase,
	.write = teds_flash_write,
	.read = teds_flash_read,
};


/**********************************************************************************************************************
 * Discovery
 **********************************************************************************************************************/

/* Probe the currently selected 1-Wire bus for any of the known EEPROM kinds. Returns the detected kind and writes the
 * device unique id into @p id, or TEDS_EEPROM_NONE when the bus is empty. Only the TMP1826 with its on-chip EEPROM is
 * recognised for now; further EEPROM kinds are tried here as they are added. */
static enum teds_eeprom_type teds_probe(Teds *self, uint8_t id[TEDS_ID_SIZE]) {
	if (tmp1826_probe(self->ow, id) == TMP1826_RET_OK) {
		return TEDS_EEPROM_TMP1826;
	}

	return TEDS_EEPROM_NONE;
}


/* Instantiate the EEPROM driver matching the channel's discovered type and point the channel's proxy target at the
 * driver's Flash interface. The mux must already be steered at the channel. */
static teds_ret_t teds_channel_open(struct teds_channel *ch) {
	switch (ch->type) {
		case TEDS_EEPROM_TMP1826:
			if (tmp1826_init(&ch->eeprom.tmp1826, ch->parent->ow) != TMP1826_RET_OK) {
				return TEDS_RET_FAILED;
			}
			tmp1826_get_flash(&ch->eeprom.tmp1826, &ch->target);
			return TEDS_RET_OK;
		default:
			return TEDS_RET_FAILED;
	}
}


static void teds_task(void *p) {
	Teds *self = p;

	while (true) {
		/* Cycle the mux through all of its channels, probing each freshly powered bus for a known EEPROM. */
		for (uint32_t i = 0; i < self->channel_count; i++) {
			struct teds_channel *ch = &self->channels[i];

			xSemaphoreTake(self->lock, portMAX_DELAY);
			teds_mux_select(self, i);

			uint8_t id[TEDS_ID_SIZE] = {0};
			enum teds_eeprom_type type = teds_probe(self, id);
			if (type == TEDS_EEPROM_NONE) {
				/* Bus is empty now; re-arm detection so a device reappearing later is reported again. */
				ch->present = false;
				xSemaphoreGive(self->lock);
				continue;
			}

			/* A device answered; nothing to do if one was already discovered on this channel. */
			if (ch->present) {
				xSemaphoreGive(self->lock);
				continue;
			}

			/* Newly detected: record it and bring up the matching EEPROM driver while the mux still points here. */
			ch->type = type;
			memcpy(ch->id, id, TEDS_ID_SIZE);
			bool ok = teds_channel_open(ch) == TEDS_RET_OK;
			xSemaphoreGive(self->lock);

			if (!ok) {
				u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("channel %u: EEPROM driver init failed"), (unsigned int)i);
				continue;
			}
			ch->present = true;

			/* Export the channel's Flash proxy once so a host can reach the EEPROM through the mux. */
			if (!ch->advertised) {
				iservicelocator_add(locator, ISERVICELOCATOR_TYPE_FLASH, (Interface *)&ch->flash, ch->name);
				ch->advertised = true;
			}

			u_log(system_log, LOG_TYPE_INFO,
				U_LOG_MODULE_PREFIX("channel %u: new TEDS EEPROM discovered, id = %02x%02x%02x%02x%02x%02x%02x%02x"),
				(unsigned int)i, ch->id[7], ch->id[6], ch->id[5], ch->id[4], ch->id[3], ch->id[2], ch->id[1], ch->id[0]);
		}

		vTaskDelay(pdMS_TO_TICKS(self->probe_interval_ms));
	}
	vTaskDelete(NULL);
}


teds_ret_t teds_init(Teds *self) {
	memset(self, 0, sizeof(Teds));
	self->current_channel = UINT32_MAX;

	self->lock = xSemaphoreCreateMutex();
	if (self->lock == NULL) {
		return TEDS_RET_FAILED;
	}

	/* 1-Wire bus and the mux fanning it out to the physical connectors, both advertised by the port. */
	if (iservicelocator_query_name_type(locator, "teds", ISERVICELOCATOR_TYPE_OW, (Interface **)&self->ow) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no 'teds' 1-Wire bus, TEDS disabled"));
		return TEDS_RET_FAILED;
	}
	if (iservicelocator_query_name_type(locator, "teds-mux", ISERVICELOCATOR_TYPE_MUX, (Interface **)&self->mux) != ISERVICELOCATOR_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("no 'teds-mux', TEDS disabled"));
		return TEDS_RET_FAILED;
	}

	if (self->mux->vmt->channels == NULL || self->mux->vmt->channels(self->mux, &self->channel_count) != MUX_RET_OK) {
		u_log(system_log, LOG_TYPE_WARN, U_LOG_MODULE_PREFIX("cannot get 'teds-mux' channel count, TEDS disabled"));
		return TEDS_RET_FAILED;
	}
	if (self->channel_count > TEDS_MAX_CHANNELS) {
		u_log(system_log, LOG_TYPE_WARN,
			U_LOG_MODULE_PREFIX("channel count clamped from %u to %u"), self->channel_count, TEDS_MAX_CHANNELS);
		self->channel_count = TEDS_MAX_CHANNELS;
	}
	self->probe_interval_ms = CONFIG_APP_MEAS_GENERIC_TEDS_PROBE_INTERVAL_MS;

	/* Wire up each channel's Flash proxy up front; it stays inert until a device is discovered and its target set. */
	for (uint32_t i = 0; i < self->channel_count; i++) {
		self->channels[i].parent = self;
		self->channels[i].index = i;
		self->channels[i].flash.parent = &self->channels[i];
		self->channels[i].flash.vmt = &teds_flash_vmt;
		snprintf(self->channels[i].name, sizeof(self->channels[i].name), "teds%u", (unsigned int)i);
	}

	xTaskCreate(teds_task, "meas-teds", configMINIMAL_STACK_SIZE + 256, (void *)self, 1, &(self->task));
	if (self->task == NULL) {
		return TEDS_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("TEDS discovery on 'teds' over %u mux channels"), self->channel_count);
	return TEDS_RET_OK;
}


teds_ret_t teds_free(Teds *self) {
	(void)self;
	return TEDS_RET_OK;
}
