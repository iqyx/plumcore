/*
 * Driver for the uxb bus, master
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

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>


#define UXB_INTERFACE_ADDRESS_LEN 8
#define UXB_CONTROL_FRAME_LEN 12

#define CONTROL_FRAME_MAGIC 0x1234

enum uxb_frame_type {
	UXB_FRAME_TYPE_UNKNOWN = 256,
	UXB_FRAME_TYPE_NOP = (0x00 << 5),
	UXB_FRAME_TYPE_ASSERT_ID = (0x01 << 5),
	UXB_FRAME_TYPE_SEL_SINGLE = (0x02 << 5),
	UXB_FRAME_TYPE_SEL_FROM = (0x03 << 5),
	UXB_FRAME_TYPE_SEL_TO = (0x04 << 5),
	UXB_FRAME_TYPE_SEL_PREV = (0x05 << 5),
	UXB_FRAME_TYPE_DATA = (0x06 << 5),
	UXB_FRAME_TYPE_SEL_OR = (0 << 4),
	UXB_FRAME_TYPE_SEL_AND = (1 << 4),
};



typedef enum {
	UXB_MASTER_LOCM3_RET_OK = 0,
	UXB_MASTER_LOCM3_RET_FAILED = -1,
	UXB_MASTER_LOCM3_RET_UNKNOWN_FRAME_TYPE = -2,
	UXB_MASTER_LOCM3_RET_NO_SELECT = -3,
	UXB_MASTER_LOCM3_RET_UNKNOWN_SLOT = -4,
	UXB_MASTER_LOCM3_RET_INVALID_BUFFER = -5,
	UXB_MASTER_LOCM3_RET_TIMEOUT = -6,
	UXB_MASTER_LOCM3_RET_IGNORED = -7,
} uxb_master_locm3_ret_t;


struct uxb_master_locm3_config {
	/** libopencm3 SPI port (SPI1/SPI2/...). */
	uint32_t spi_port;

	/** alternate function number (GPIO_AF1/GPIO_AF2/...). */
	uint32_t spi_af;

	/** SPI bus ports and pins. */
	uint32_t sck_port;
	uint32_t sck_pin;
	uint32_t mosi_port;
	uint32_t mosi_pin;
	uint32_t miso_port;
	uint32_t miso_pin;

	/** SPI bus shared chip select. */
	uint32_t frame_port;
	uint32_t frame_pin;

	/** Open-drain slave wake up pin. */
	uint32_t wakeup_port;
	uint32_t wakeup_pin;

	/** Open-drain master wake-up pin (interrupt request). */
	uint32_t irq_port;
	uint32_t irq_pin;

	/** Open-drain ID pin (device discovery). */
	uint32_t id_port;
	uint32_t id_pin;

	/** Timer used to control protocol timing. */
	uint32_t delay_timer;
	uint32_t delay_timer_freq_mhz;

	uint32_t control_prescaler;
	uint32_t data_prescaler;
};



/* Forward declarations. */
struct libuxb_device;
struct libuxb_slot;


typedef struct {
	struct uxb_master_locm3_config config;

	struct libuxb_device *devices;

	/**
	 * Last interface used for sending data. It is used for matching the response.
	 */
	struct libuxb_device *previous_device;

	uint8_t control_frame[UXB_CONTROL_FRAME_LEN];
	volatile bool ignore_rx;
} LibUxbBus;


/**
 * Device on the UXB bus is defined by a 64bit address.
 */
typedef struct libuxb_device {
	uint8_t local_address[UXB_INTERFACE_ADDRESS_LEN];
	uint8_t remote_address[UXB_INTERFACE_ADDRESS_LEN];
	bool selected;

	struct libuxb_slot *slots;

	LibUxbBus *parent;
	struct libuxb_device *next;
} LibUxbDevice;


/**
 * Slot is a data pipe within the interface able to transmit and receive
 * data. It is specified by a 8bit slot number.
 */
typedef struct libuxb_slot {
	uint8_t slot_number;

	/* Buffer used to send and receive data. Must by serviceable by the DMA. */
	uint8_t *buffer;
	size_t size;
	size_t len;

	/* Optionally, the application can be notified when data arrives. */
	uxb_master_locm3_ret_t (*data_received)(void *context, uint8_t *buf, size_t len);
	void *callback_context;

	LibUxbDevice *parent;
	struct libuxb_slot *next;
} LibUxbSlot;


uxb_master_locm3_ret_t libuxb_bus_init(LibUxbBus *self, const struct uxb_master_locm3_config *config);
uxb_master_locm3_ret_t libuxb_bus_frame_irq(LibUxbBus *self);
uxb_master_locm3_ret_t libuxb_bus_add_device(LibUxbBus *self, LibUxbDevice *i);
uxb_master_locm3_ret_t libuxb_bus_check_id(LibUxbBus *self, uint8_t id[UXB_INTERFACE_ADDRESS_LEN], bool *result);
uxb_master_locm3_ret_t libuxb_bus_query_device_by_id(LibUxbBus *self, uint8_t id[UXB_INTERFACE_ADDRESS_LEN], LibUxbDevice **result);

uxb_master_locm3_ret_t libuxb_device_init(LibUxbDevice *self);
uxb_master_locm3_ret_t libuxb_device_add_slot(LibUxbDevice *self, LibUxbSlot *s);
uxb_master_locm3_ret_t libuxb_device_set_address(LibUxbDevice *self, const uint8_t local_address[UXB_INTERFACE_ADDRESS_LEN], const uint8_t remote_address[UXB_INTERFACE_ADDRESS_LEN]);

uxb_master_locm3_ret_t libuxb_slot_init(LibUxbSlot *self);
uxb_master_locm3_ret_t libuxb_slot_set_slot_number(LibUxbSlot *self, uint8_t slot_number);
uxb_master_locm3_ret_t libuxb_slot_set_slot_buffer(LibUxbSlot *self, uint8_t *buf, size_t size);
uxb_master_locm3_ret_t libuxb_slot_set_data_received(LibUxbSlot *self, uxb_master_locm3_ret_t (*data_received)(void *context, uint8_t *buf, size_t len), void *context);
uxb_master_locm3_ret_t libuxb_slot_send_data(LibUxbSlot *self, const uint8_t *buf, size_t len, bool response);
uxb_master_locm3_ret_t libuxb_slot_receive_data(LibUxbSlot *self);
