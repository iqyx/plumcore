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

enum uxb_control_frame_type {
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

	/** Timer used to control protocol timing. */
	uint32_t delay_timer;
	uint32_t delay_timer_freq_mhz;

	uint32_t control_prescaler;
	uint32_t data_prescaler;
};



/* Forward declarations. */
struct uxb_master_locm3_interface;
struct uxb_master_locm3_slot;


typedef struct {
	struct uxb_master_locm3_config config;

	struct uxb_master_locm3_interface *interfaces;

	/**
	 * Last interface used for sending data. It is used for matching the response.
	 */
	struct uxb_master_locm3_interface *previous_interface;

	uint8_t control_frame[UXB_CONTROL_FRAME_LEN];
	bool ignore_rx;
} UxbMasterLocm3;


/**
 * Interface on a UXB bus is defined by a 64bit address. A single device
 * on the bus may have multiple interfaces.
 */
typedef struct uxb_master_locm3_interface {
	uint8_t local_address[UXB_INTERFACE_ADDRESS_LEN];
	uint8_t remote_address[UXB_INTERFACE_ADDRESS_LEN];
	bool selected;

	struct uxb_master_locm3_slot *slots;

	UxbMasterLocm3 *parent;
	struct uxb_master_locm3_interface *next;
} UxbInterface;


/**
 * Slot is a data pipe within the interface able to transmit and receive
 * data. It is specified by a 8bit slot number.
 */
typedef struct uxb_master_locm3_slot {
	uint8_t slot_number;

	/* Buffer used to send and receive data. Must by serviceable by the DMA. */
	uint8_t *buffer;
	size_t size;
	size_t len;

	/* Optionally, the application can be notified when data arrives. */
	uxb_master_locm3_ret_t (*data_received)(void *context, uint8_t *buf, size_t len);
	void *callback_context;

	UxbInterface *parent;
	struct uxb_master_locm3_slot *next;
} UxbSlot;


uxb_master_locm3_ret_t uxb_master_locm3_init(UxbMasterLocm3 *self, const struct uxb_master_locm3_config *config);
uxb_master_locm3_ret_t uxb_master_locm3_frame_irq(UxbMasterLocm3 *self);
uxb_master_locm3_ret_t uxb_master_locm3_add_interface(UxbMasterLocm3 *self, UxbInterface *i);

uxb_master_locm3_ret_t uxb_interface_init(UxbInterface *self);
uxb_master_locm3_ret_t uxb_interface_add_slot(UxbInterface *self, UxbSlot *s);
uxb_master_locm3_ret_t uxb_interface_set_address(UxbInterface *self, const uint8_t local_address[UXB_INTERFACE_ADDRESS_LEN], const uint8_t remote_address[UXB_INTERFACE_ADDRESS_LEN]);

uxb_master_locm3_ret_t uxb_slot_init(UxbSlot *self);
uxb_master_locm3_ret_t uxb_slot_set_slot_number(UxbSlot *self, uint8_t slot_number);
uxb_master_locm3_ret_t uxb_slot_set_slot_buffer(UxbSlot *self, uint8_t *buf, size_t size);
uxb_master_locm3_ret_t uxb_slot_set_data_received(UxbSlot *self, uxb_master_locm3_ret_t (*data_received)(void *context, uint8_t *buf, size_t len), void *context);
uxb_master_locm3_ret_t uxb_slot_send_data(UxbSlot *self, const uint8_t *buf, size_t len, bool response);
uxb_master_locm3_ret_t uxb_slot_receive_data(UxbSlot *self);
