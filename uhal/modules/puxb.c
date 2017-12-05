/*
 * uHal module implementing IUxbBus, IUxbDevice and IUxbSlot interfaces
 * using the libuxb library (proxy class)
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/projects/umesh)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "u_assert.h"
#include "u_log.h"

#include "module.h"
#include "libuxb.h"
#include "puxb.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "puxb"

static iuxbslot_ret_t puxb_slot_receive(void *context, uint8_t * buf, size_t size, size_t * len) {
	PUxbSlot *slot = (PUxbSlot *)context;
	xSemaphoreTake(slot->read_lock, portMAX_DELAY);

	if (slot->slot.len != 0 && slot->slot.len <= size) {
		memcpy(buf, slot->slot.buffer, slot->slot.len);
		*len = slot->slot.len;
		return IUXBSLOT_RET_OK;
	}

	return IUXBSLOT_RET_FAILED;
}


static uxb_master_locm3_ret_t puxb_slot_data_received(void *context, uint8_t *buf, size_t len) {
	(void)buf;
	(void)len;
	PUxbSlot *slot = (PUxbSlot *)context;

	/* The callback of the libuxb is called from the interrupt. */
	xSemaphoreGiveFromISR(slot->read_lock, NULL);
	return UXB_MASTER_LOCM3_RET_OK;
}


static iuxbslot_ret_t puxb_slot_send(void *context, const uint8_t * buf, size_t len, bool response) {
	PUxbSlot *slot = (PUxbSlot *)context;
	libuxb_slot_send_data(&slot->slot, buf, len, response);

	return IUXBSLOT_RET_OK;
}


static iuxbslot_ret_t puxb_slot_set_slot_number(void *context, uint8_t slot_number) {
	PUxbSlot *slot = (PUxbSlot *)context;
	libuxb_slot_set_slot_number(&slot->slot, slot_number);

	return IUXBSLOT_RET_OK;
}


static iuxbslot_ret_t puxb_slot_set_slot_buffer(void *context, uint8_t * buf, size_t len) {
	PUxbSlot *slot = (PUxbSlot *)context;
	libuxb_slot_set_slot_buffer(&slot->slot, buf, len);

	return IUXBSLOT_RET_OK;
}


static iuxbdevice_ret_t puxb_device_add_slot(void *context, IUxbSlot ** slot) {
	PUxbDevice *device = (PUxbDevice *)context;

	PUxbSlot *new_slot = malloc(sizeof(PUxbSlot));
	if (new_slot == NULL) {
		return IUXBDEVICE_RET_FAILED;
	}
	memset(new_slot, 0, sizeof(PUxbSlot));

	new_slot->read_lock = xSemaphoreCreateBinary();
	if (new_slot->read_lock == NULL) {
		return IUXBDEVICE_RET_FAILED;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("lock = %08x"), new_slot->read_lock);

	libuxb_slot_init(&new_slot->slot);
	libuxb_slot_set_data_received(&new_slot->slot, puxb_slot_data_received, (void *)new_slot);
	libuxb_device_add_slot(&device->device, &new_slot->slot);

	iuxbslot_init(&new_slot->slot_iface);
	new_slot->slot_iface.vmt.context = (void *)new_slot;
	new_slot->slot_iface.vmt.set_slot_buffer = puxb_slot_set_slot_buffer;
	new_slot->slot_iface.vmt.set_slot_number = puxb_slot_set_slot_number;
	new_slot->slot_iface.vmt.send = puxb_slot_send;
	new_slot->slot_iface.vmt.receive = puxb_slot_receive;


	new_slot->next = device->slots;
	new_slot->parent = device;
	device->slots = new_slot;

	*slot = &new_slot->slot_iface;

	return IUXBDEVICE_RET_OK;
}


static iuxbdevice_ret_t puxb_device_remove_slot(void *context, IUxbSlot * slot) {
	(void)context;
	(void)slot;

	return IUXBDEVICE_RET_FAILED;
}


static iuxbdevice_ret_t puxb_device_get_name(void *context, char ** buf) {
	PUxbDevice *self = (PUxbDevice *)context;
	*buf = self->device_name;
	return IUXBDEVICE_RET_OK;
}


static uxb_master_locm3_ret_t puxb_descriptor_data_received(void *context, uint8_t *buf, size_t len) {
	(void)buf;
	(void)len;
	PUxbDevice *device = (PUxbDevice *)context;
	xSemaphoreGiveFromISR(device->descriptor_lock, NULL);
	return UXB_MASTER_LOCM3_RET_OK;
}


static puxb_ret_t puxb_device_parse_descriptor(PUxbDevice *self, const char *buf, size_t len, char *key, size_t key_size, char *value, size_t value_size) {
	(void)self;

	if (key == NULL || value == NULL) {
		return PUXB_RET_FAILED;
	}

	size_t i = 0;
	while (*buf != '=' && len > 0) {
		if (i < (key_size - 1)) {
			key[i] = *buf;
			i++;
		}
		buf++;
		len--;
	}
	key[i] = '\0';

	if (*buf == '=') {
		buf++;
		len--;
	} else {
		return PUXB_RET_FAILED;
	}

	i = 0;
	while (len > 0) {
		if (i < (value_size - 1)) {
			value[i] = *buf;
			i++;
		}

		buf++;
		len--;
	}
	value[i] = '\0';

	return PUXB_RET_OK;
}


static puxb_ret_t puxb_device_process_descriptor(PUxbDevice *self, const char *key, const char *value) {
	if (!strcmp(key, "device")) {
		strlcpy(self->device_name, value, PUXB_DESCRIPTOR_DEVICE_NAME_SIZE);
	} else if (!strcmp(key, "hw-version")) {
		strlcpy(self->hw_version, value, PUXB_DESCRIPTOR_VERSION_SIZE);
	} else if (!strcmp(key, "fw-version")) {
		strlcpy(self->fw_version, value, PUXB_DESCRIPTOR_VERSION_SIZE);
	} else {
		return PUXB_RET_FAILED;
	}
	return PUXB_RET_OK;
}


static iuxbdevice_ret_t puxb_device_read_descriptor(void *context, uint8_t line, char *key, size_t key_size, char *value, size_t value_size) {
	PUxbDevice *device = (PUxbDevice *)context;

	if (device->descriptor == NULL) {
		device->descriptor = (LibUxbSlot *)malloc(sizeof(LibUxbSlot));
	}
	if (device->descriptor == NULL) {
		return IUXBDEVICE_RET_FAILED;
	}
	if (device->descriptor_lock == NULL) {
		device->descriptor_lock = xSemaphoreCreateBinary();
	}
	if (device->descriptor_lock == NULL) {
		return IUXBDEVICE_RET_FAILED;
	}

	libuxb_slot_init(device->descriptor);
	libuxb_slot_set_slot_number(device->descriptor, 0);
	libuxb_slot_set_slot_buffer(device->descriptor, device->descriptor_buffer, PUXB_DESCRIPTOR_BUFFER_SIZE);
	libuxb_slot_set_data_received(device->descriptor, puxb_descriptor_data_received, (void *)device);
	libuxb_device_add_slot(&device->device, device->descriptor);

	libuxb_slot_send_data(device->descriptor, &line, 1, false);
	xSemaphoreTake(device->descriptor_lock, portMAX_DELAY);
	if (device->descriptor->len == 1 && device->descriptor->buffer[0] == 0) {
		return IUXBDEVICE_RET_FAILED;
	}

	char m_key[PUXB_DESCRIPTOR_KEY_SIZE];
	char m_value[PUXB_DESCRIPTOR_VALUE_SIZE];
	puxb_device_parse_descriptor(device, (char *)device->descriptor->buffer, device->descriptor->len, m_key, PUXB_DESCRIPTOR_KEY_SIZE, m_value, PUXB_DESCRIPTOR_VALUE_SIZE);
	puxb_device_process_descriptor(device, key, value);
	strlcpy(key, m_key, key_size);
	strlcpy(value, m_value, value_size);

	return IUXBDEVICE_RET_OK;
}


static iuxbdevice_ret_t puxb_device_set_address(void *context, uint8_t * local_address, uint8_t * remote_address) {
	PUxbDevice *device = (PUxbDevice *)context;

	libuxb_device_set_address(&device->device, local_address, remote_address);
	return IUXBDEVICE_RET_FAILED;
}


static iuxbbus_ret_t puxb_bus_probe_device(void *context, uint8_t * address, bool * result) {
	PUxbBus *self = (PUxbBus *)context;
	libuxb_bus_check_id(&self->bus, address, result);

	return IUXBBUS_RET_OK;
}


static iuxbbus_ret_t puxb_bus_add_device(void *context, IUxbDevice ** device) {
	PUxbBus *self = (PUxbBus *)context;

	PUxbDevice *new_device = malloc(sizeof(PUxbDevice));
	if (new_device == NULL) {
		return IUXBBUS_RET_FAILED;
	}
	memset(new_device, 0, sizeof(PUxbDevice));
	libuxb_device_init(&new_device->device);
	libuxb_bus_add_device(&self->bus, &new_device->device);

	iuxbdevice_init(&new_device->device_iface);
	new_device->device_iface.vmt.context = (void *)new_device;
	new_device->device_iface.vmt.add_slot = puxb_device_add_slot;
	new_device->device_iface.vmt.remove_slot = puxb_device_remove_slot;
	new_device->device_iface.vmt.set_address = puxb_device_set_address;
	new_device->device_iface.vmt.read_descriptor = puxb_device_read_descriptor;
	new_device->device_iface.vmt.get_name = puxb_device_get_name;

	new_device->next = self->devices;
	new_device->parent = self;
	self->devices = new_device;

	*device = &new_device->device_iface;

	return IUXBBUS_RET_OK;
}


puxb_ret_t puxb_bus_init(PUxbBus *self, const struct uxb_master_locm3_config *config) {

	memset(self, 0, sizeof(PUxbBus));

	libuxb_bus_init(&self->bus, config);

	iuxbbus_init(&self->iface);
	self->iface.vmt.context = (void *)self;
	self->iface.vmt.probe_device = puxb_bus_probe_device;
	self->iface.vmt.add_device = puxb_bus_add_device;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module initialized"));
	return PUXB_RET_OK;
}


IUxbBus *puxb_bus_interface(PUxbBus *self) {
	return &self->iface;
}
