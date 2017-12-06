/*
 * Driver for the UXB bus using the libopencm3 library
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
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
#include <stdbool.h>
#include <string.h>

/* System includes */
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/rcc.h>

#include "libuxb.h"


static uint16_t timer_wait_init(LibUxbBus *self) {
	return timer_get_counter(self->config.delay_timer);
}


static void timer_wait(LibUxbBus *self, uint16_t t, uint16_t interval) {
	interval *= self->config.delay_timer_freq_mhz;

	uint16_t diff;
	do {
		uint16_t n = timer_get_counter(self->config.delay_timer);
		/* uint16_t subtraction with overflow. */
		diff = n - t;
	} while (diff <= interval);
}


static bool timer_wait_check(LibUxbBus *self, uint16_t t, uint16_t interval) {
	interval *= self->config.delay_timer_freq_mhz;
	uint16_t n = timer_get_counter(self->config.delay_timer);
	/* uint16_t subtraction with overflow. */
	uint16_t diff = n - t;
	return diff > interval;
}


/**
 * @brief Reconfigure the SPI port for transmission or reception
 *
 * @return void, by design it cannot fail if the peripheral clock and timer is running properly.
 */
static void reconfigure_spi_port(LibUxbBus *self, bool transmit) {

	if (transmit) {
		/* Inter frame-group minimum delay. */
		uint16_t t = timer_wait_init(self);
		timer_wait(self, t, 300);
	}

	/* Drive the SPI port, disable pull-ups to save some power. */
	gpio_mode_setup(self->config.sck_port, GPIO_MODE_AF, GPIO_PUPD_NONE, self->config.sck_pin);
	gpio_mode_setup(self->config.frame_port, GPIO_MODE_AF, GPIO_PUPD_PULLUP, self->config.frame_pin);
	gpio_mode_setup(self->config.mosi_port, GPIO_MODE_AF, GPIO_PUPD_NONE, self->config.mosi_pin);
	gpio_mode_setup(self->config.miso_port, GPIO_MODE_AF, GPIO_PUPD_NONE, self->config.miso_pin);

	if (transmit) {
		/* Order of the following statements is important. */
		SPI_CR1(self->config.spi_port) |= SPI_CR1_BIDIOE;
		SPI_CR1(self->config.spi_port) |= SPI_CR1_MSTR;
		SPI_CR1(self->config.spi_port) &= ~SPI_CR1_SSM;
		SPI_CR2(self->config.spi_port) |= SPI_CR2_SSOE;

	} else {
		/* Select software slave management and turn the NSS input low to
		 * reset the receiver shift register. */
		SPI_CR1(self->config.spi_port) &= ~SPI_CR1_SSI;
		SPI_CR1(self->config.spi_port) |= SPI_CR1_SSM;

		SPI_CR1(self->config.spi_port) &= ~SPI_CR1_MSTR;
		SPI_CR1(self->config.spi_port) &= ~SPI_CR1_BIDIOE;
	}

	SPI_CR1(self->config.spi_port) |= SPI_CR1_SPE;

	if (transmit) {
		/* Frame-group start minimum delay (frame to data). */
		uint16_t t = timer_wait_init(self);
		timer_wait(self, t, 200);
	}
}


static uxb_master_locm3_ret_t release_spi_port(LibUxbBus *self, bool wait_for_bus_free) {
	/* Make the port idle, enable pull resistor to define the idle state and turn off the SPI port. */
	gpio_mode_setup(self->config.miso_port, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, self->config.miso_pin);
	gpio_mode_setup(self->config.mosi_port, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, self->config.mosi_pin);
	gpio_mode_setup(self->config.sck_port, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, self->config.sck_pin);
	gpio_mode_setup(self->config.frame_port, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, self->config.frame_pin);

	/* If configured as master, leave as it is and disable. If configured as slave,
	 * deassert NSS and disable. */
	SPI_CR1(self->config.spi_port) |= SPI_CR1_SSI;
	SPI_CR1(self->config.spi_port) &= ~SPI_CR1_SPE;

	/* De-assert the ID line too. */
	gpio_set(self->config.id_port, self->config.id_pin);

	#if defined(STM32F3) || defined(STM32F0)
		/* And read the rest of data (to clear the FIFO). */
		while (SPI_SR(self->config.spi_port) & SPI_SR_FRLVL_FIFO_FULL) {
			spi_read(self->config.spi_port);
		}
	#endif

	if (wait_for_bus_free) {
		/** @todo timeout */
		/* Wait for the frame signal to go back up. */
		while (gpio_get(self->config.frame_port, self->config.frame_pin) == 0) {
			;
		}
	}

	return UXB_MASTER_LOCM3_RET_OK;
}


static void spi_send_data(LibUxbBus *self, uint8_t *buf, size_t len) {
	for (size_t i = 0; i < len; i++) {
		while (!(SPI_SR(self->config.spi_port) & SPI_SR_TXE)) {
			;
		}
		#if defined(STM32F3) || defined(STM32F0)
			SPI_DR8(self->config.spi_port) = buf[i];
		#else
			SPI_DR(self->config.spi_port) = buf[i];
		#endif
	}

	while (SPI_SR(self->config.spi_port) & SPI_SR_BSY) {
		;
	}

	/* Respect inter-frame delay. */
	uint16_t t = timer_wait_init(self);
	timer_wait(self, t, 100);
}


static uxb_master_locm3_ret_t spi_recv_data(LibUxbBus *self, uint8_t *buf, size_t len, uint16_t start_timeout, uint16_t byte_timeout) {

	uint16_t t = timer_wait_init(self);
	while (!(SPI_SR(self->config.spi_port) & SPI_SR_RXNE)) {
		if (timer_wait_check(self, t, start_timeout)) {
			break;
		}
	}

	for (size_t i = 0; i < len; i++) {
		t = timer_wait_init(self);
		while (!(SPI_SR(self->config.spi_port) & SPI_SR_RXNE)) {
			if (timer_wait_check(self, t, byte_timeout)) {
				break;
			}
		}
		buf[i] = SPI_DR(self->config.spi_port);
	}

	return UXB_MASTER_LOCM3_RET_OK;
}


static uxb_master_locm3_ret_t uxb_build_nop_control(uint8_t *buf) {
    memcpy(buf, (uint8_t []) {
        CONTROL_FRAME_MAGIC >> 8,
        CONTROL_FRAME_MAGIC & 0xff,
        UXB_FRAME_TYPE_NOP,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    }, 12);

    return UXB_MASTER_LOCM3_RET_OK;
}


static uxb_master_locm3_ret_t uxb_build_id_assert_control(uint8_t *buf) {
    memcpy(buf, (uint8_t []) {
        CONTROL_FRAME_MAGIC >> 8,
        CONTROL_FRAME_MAGIC & 0xff,
        UXB_FRAME_TYPE_ASSERT_ID,
        0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    }, 12);

    return UXB_MASTER_LOCM3_RET_OK;
}


static uxb_master_locm3_ret_t uxb_build_select_single_control(uint8_t *buf, uint8_t addr[8], bool sel_and) {

    memcpy(buf, (uint8_t []) {
        CONTROL_FRAME_MAGIC >> 8,
        CONTROL_FRAME_MAGIC & 0xff,
        UXB_FRAME_TYPE_SEL_SINGLE | (sel_and ? UXB_FRAME_TYPE_SEL_AND : UXB_FRAME_TYPE_SEL_OR),
        0x00,
	addr[0],
	addr[1],
	addr[2],
	addr[3],
	addr[4],
	addr[5],
	addr[6],
	addr[7],
    }, 12);

    return UXB_MASTER_LOCM3_RET_OK;
}


static uxb_master_locm3_ret_t uxb_build_select_response_control(uint8_t *buf) {

    memcpy(buf, (uint8_t []) {
        CONTROL_FRAME_MAGIC >> 8,
        CONTROL_FRAME_MAGIC & 0xff,
        UXB_FRAME_TYPE_SEL_PREV,
        0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
    }, 12);

    return UXB_MASTER_LOCM3_RET_OK;
}


static uxb_master_locm3_ret_t uxb_build_data_control(uint8_t *buf, uint16_t len, uint8_t slot, uint32_t crc32) {

    memcpy(buf, (uint8_t []) {
        CONTROL_FRAME_MAGIC >> 8,
        CONTROL_FRAME_MAGIC & 0xff,
        UXB_FRAME_TYPE_DATA,
        0x00,
	(len >> 8) & 0xff,
	(len >> 0) & 0xff,
	slot,
	0x00,
	(crc32 >> 24) & 0xff,
	(crc32 >> 16) & 0xff,
	(crc32 >> 8) & 0xff,
	(crc32 >> 0) & 0xff,
    }, 12);

    return UXB_MASTER_LOCM3_RET_OK;
}


static enum uxb_frame_type get_control_frame_type(uint8_t *frame) {
	if ((frame[0] << 8 | frame[1]) != CONTROL_FRAME_MAGIC) {
		return UXB_FRAME_TYPE_UNKNOWN;
	}

	/* Mask the first 3 bits = frme type. */
	return frame[2] & 0xe0;
}


static uint8_t get_data_slot(uint8_t *frame) {
	return frame[6];
}


static uint16_t get_data_len(uint8_t *frame) {
	return (frame[4] << 8) | frame[5];
}


uxb_master_locm3_ret_t libuxb_bus_init(LibUxbBus *self, const struct uxb_master_locm3_config *config) {

	memset(self, 0, sizeof(LibUxbBus));
	memcpy(&self->config, config, sizeof(struct uxb_master_locm3_config));

	/* Setup SPI pin mode and AF number, but does not connecte them to the AF, leave as input. */
	gpio_set_output_options(self->config.miso_port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, self->config.miso_pin);
	gpio_set_af(self->config.miso_port, self->config.spi_af, self->config.miso_pin);

	gpio_set_output_options(self->config.mosi_port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, self->config.mosi_pin);
	gpio_set_af(self->config.mosi_port, self->config.spi_af, self->config.mosi_pin);

	gpio_set_output_options(self->config.sck_port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, self->config.sck_pin);
	gpio_set_af(self->config.sck_port, self->config.spi_af, self->config.sck_pin);

	gpio_set_output_options(self->config.frame_port, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, self->config.frame_pin);
	gpio_set_af(self->config.frame_port, self->config.spi_af, self->config.frame_pin);

	gpio_mode_setup(self->config.id_port, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, self->config.id_pin);
	gpio_set_output_options(self->config.id_port, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ, self->config.id_pin);
	gpio_set(self->config.id_port, self->config.id_pin);

	SPI_CR1(self->config.spi_port) |= SPI_CR1_BIDIMODE;
	/* Data frame format is 8 bit (default). */
	spi_set_baudrate_prescaler(self->config.spi_port, SPI_CR1_BR_FPCLK_DIV_4);

	#if defined(STM32F3) || defined(STM32F0)
		spi_set_data_size(self->config.spi_port, SPI_CR2_DS_8BIT);
		SPI_CR2(self->config.spi_port) |= SPI_CR2_FRXTH;
	#endif
	#if defined(STM32F4)
		SPI_CR1(self->config.spi_port) &= -SPI_CR1_DFF;
	#endif

	/* Configure the SPI port as inputs with pull-ups (port is in the idle state). */
	if (release_spi_port(self, false) != UXB_MASTER_LOCM3_RET_OK) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_bus_frame_irq(LibUxbBus *self) {
	if (self == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	if (self->ignore_rx) {
		return UXB_MASTER_LOCM3_RET_IGNORED;
	}

	uxb_master_locm3_ret_t ret = UXB_MASTER_LOCM3_RET_FAILED;

	/* Reconfigure the port immediately for reception. We have 100us
	 * according to the UXB specification, otherwise we may miss some data. */
	reconfigure_spi_port(self, false);

	/* Deselect all interfaces. */
	{
		LibUxbDevice *i = self->devices;
		while (i != NULL) {
			i->selected = false;
			i = i->next;
		}
	}

	while (true) {
		/* Receive a frame. All frames are control frames except the last
		 * one if marked as a data frame by the previous control frame. */
		if (spi_recv_data(self, self->control_frame, UXB_CONTROL_FRAME_LEN, 1000, 20) != UXB_MASTER_LOCM3_RET_OK) {
			goto err;
		}

		switch (get_control_frame_type(self->control_frame)) {

			case UXB_FRAME_TYPE_SEL_SINGLE: {

				/* Find an interface with a corresponding address. */
				LibUxbDevice *i = self->devices;
				while (i != NULL) {
					if (!memcmp(&(self->control_frame[4]), &(i->local_address), UXB_INTERFACE_ADDRESS_LEN)) {
						i->selected = true;
					}
					i = i->next;
				}

				break;
			}

			case UXB_FRAME_TYPE_SEL_PREV: {
				self->previous_device->selected = true;
				break;

			}

			case UXB_FRAME_TYPE_ASSERT_ID: {

				/* Find the first selected interface. */
				LibUxbDevice *selected_device = NULL;
				LibUxbDevice *i = self->devices;
				while (i != NULL) {
					if (i->selected) {
						selected_device = i;
						break;
					}
					i = i->next;
				}
				if (selected_device != NULL) {
					gpio_clear(self->config.id_port, self->config.id_pin);
				}

				break;
			}

			case UXB_FRAME_TYPE_NOP: {
				if (release_spi_port(self, true) != UXB_MASTER_LOCM3_RET_OK) {
					ret = UXB_MASTER_LOCM3_RET_TIMEOUT;
					goto err;
				}

				/* Do not receive any further frames. */
				return UXB_MASTER_LOCM3_RET_OK;
			}

			case UXB_FRAME_TYPE_DATA: {

				/* Find the first selected interface. */
				LibUxbDevice *selected_device = NULL;
				LibUxbDevice *i = self->devices;
				while (i != NULL) {
					if (i->selected) {
						selected_device = i;
						break;
					}
					i = i->next;
				}
				if (selected_device == NULL) {
					ret = UXB_MASTER_LOCM3_RET_NO_SELECT;
					goto err;
				}

				uint8_t slot = get_data_slot(self->control_frame);
				uint16_t len = get_data_len(self->control_frame);


				LibUxbSlot *selected_slot = NULL;
				LibUxbSlot *s = selected_device->slots;
				while (s != NULL) {
					if (s->slot_number == slot) {
						selected_slot = s;
						break;
					}
					s = s->next;
				}

				if (selected_slot == NULL) {
					ret = UXB_MASTER_LOCM3_RET_UNKNOWN_SLOT;
					goto err;
				}
				if (selected_slot->buffer == NULL || selected_slot->size < len) {
					ret = UXB_MASTER_LOCM3_RET_INVALID_BUFFER;
					goto err;
				}
				selected_slot->len = len;

				if (spi_recv_data(self, selected_slot->buffer, len, 1000, 20) != UXB_MASTER_LOCM3_RET_OK) {
					ret = UXB_MASTER_LOCM3_RET_TIMEOUT;
					goto err;
				}

				/* Release the port now to allow the callback to send data back. Wait for the frame
				 * signal to go back up. */
				if (release_spi_port(self, true) != UXB_MASTER_LOCM3_RET_OK) {
					/* The frame was not finished within the specified timeout. */
					ret = UXB_MASTER_LOCM3_RET_TIMEOUT;
					goto err;
				}

				if (selected_slot->data_received != NULL) {
					selected_slot->data_received(selected_slot->callback_context, selected_slot->buffer, len);
				}

				/* This is the single exit point without an error (full frame was received correctly). */
				release_spi_port(self, false);
				return UXB_MASTER_LOCM3_RET_OK;
			}

			default:
				/* Incorrect frame type, stop the whole frame-group. */
				ret = UXB_MASTER_LOCM3_RET_UNKNOWN_FRAME_TYPE;
				goto err;
				break;
		}

	}

err:
	/* Do not process the return value. */
	release_spi_port(self, false);
	return ret;
}


uxb_master_locm3_ret_t libuxb_bus_add_device(LibUxbBus *self, LibUxbDevice *i) {
	if (self == NULL || i == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	i->parent = self;
	i->next = self->devices;
	self->devices = i;

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_bus_check_id(LibUxbBus *self, uint8_t id[UXB_INTERFACE_ADDRESS_LEN], bool *result) {
	if (self == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	self->ignore_rx = true;
	reconfigure_spi_port(self, true);

	spi_set_baudrate_prescaler(self->config.spi_port, self->config.control_prescaler);
	uint8_t frame[12];
	uxb_build_select_single_control(frame, id, false);
	spi_send_data(self, frame, 12);

	uxb_build_id_assert_control(frame);
	spi_send_data(self, frame, 12);

	/* Send the frame two times to allow the ID signal to stabilize. */
	/** @todo meh. */
	spi_send_data(self, frame, 12);

	*result = !gpio_get(self->config.id_port, self->config.id_pin);

	uxb_build_nop_control(frame);
	spi_send_data(self, frame, 12);

	release_spi_port(self, false);
	self->ignore_rx = false;

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_bus_query_device_by_id(LibUxbBus *self, uint8_t id[UXB_INTERFACE_ADDRESS_LEN], LibUxbDevice **result) {
	if (self == NULL || result == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	LibUxbDevice *d = self->devices;
	while (d != NULL) {
		if (!memcmp(id, d->remote_address, UXB_INTERFACE_ADDRESS_LEN)) {
			*result = d;
			return UXB_MASTER_LOCM3_RET_OK;
		}
		d = d->next;
	}

	return UXB_MASTER_LOCM3_RET_FAILED;
}


uxb_master_locm3_ret_t libuxb_device_init(LibUxbDevice *self) {
	if (self == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	memset(self, 0, sizeof(LibUxbDevice));

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_device_add_slot(LibUxbDevice *self, LibUxbSlot *s) {
	if (self == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	s->parent = self;
	s->next = self->slots;
	self->slots = s;

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_device_set_address(LibUxbDevice *self, const uint8_t local_address[UXB_INTERFACE_ADDRESS_LEN], const uint8_t remote_address[UXB_INTERFACE_ADDRESS_LEN]) {
	if (self == NULL || local_address == NULL || remote_address == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	memcpy(&self->local_address, local_address, UXB_INTERFACE_ADDRESS_LEN);
	memcpy(&self->remote_address, remote_address, UXB_INTERFACE_ADDRESS_LEN);

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_slot_init(LibUxbSlot *self) {
	if (self == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	memset(self, 0, sizeof(LibUxbSlot));

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_slot_set_slot_number(LibUxbSlot *self, uint8_t slot_number) {
	/* Slot number 0 is reserved for the interface descriptor. */
	if (self == NULL || slot_number == 0) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	self->slot_number = slot_number;

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_slot_set_slot_buffer(LibUxbSlot *self, uint8_t *buf, size_t size) {
	/* The buffer can be NULL, in this case no data can be received or sent. The slot
	 * can be still used for polling/command execution. */
	if (self == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	self->buffer = buf;
	self->size = size;

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_slot_set_data_received(LibUxbSlot *self, uxb_master_locm3_ret_t (*data_received)(void *context, uint8_t *buf, size_t len), void *context) {
	if (self == NULL) {
		return UXB_MASTER_LOCM3_RET_FAILED;
	}

	self->data_received = data_received;
	self->callback_context = context;

	return UXB_MASTER_LOCM3_RET_OK;
}


uxb_master_locm3_ret_t libuxb_slot_send_data(LibUxbSlot *self, const uint8_t *buf, size_t len, bool response) {
	LibUxbDevice *device = self->parent;
	LibUxbBus *bus = device->parent;

	bus->ignore_rx = true;

	/* Copy data to the slot buffer first. */
	if (len > self->size) {
		return UXB_MASTER_LOCM3_RET_INVALID_BUFFER;
	}
	memcpy(self->buffer, buf, len);
	self->len = len;

	/** @todo compute crc */

	reconfigure_spi_port(bus, true);

	spi_set_baudrate_prescaler(bus->config.spi_port, bus->config.control_prescaler);
	uint8_t frame[12];
	if (response) {
		uxb_build_select_response_control(frame);
	} else {
		uxb_build_select_single_control(frame, device->remote_address, false);
		bus->previous_device = device;
	}
	spi_send_data(bus, frame, 12);

	uxb_build_data_control(frame, self->len, self->slot_number, 0);
	spi_send_data(bus, frame, 12);

	spi_set_baudrate_prescaler(bus->config.spi_port, bus->config.data_prescaler);
	spi_send_data(bus, self->buffer, self->len);
	release_spi_port(bus, false);

	bus->ignore_rx = false;

	return UXB_MASTER_LOCM3_RET_OK;
}




uxb_master_locm3_ret_t libuxb_slot_receive_data(LibUxbSlot *self) {
	(void)self;

	return UXB_MASTER_LOCM3_RET_OK;
}

