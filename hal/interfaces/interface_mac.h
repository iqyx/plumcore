/**
 * uMeshFw HAL MAC interface
 *
 * Copyright (C) 2015, Marek Koza, qyx@krtko.org
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

#ifndef _INTERFACE_MAC_H_
#define _INTERFACE_MAC_H_

#include <stdint.h>
#include "hal_interface.h"
#include "interface_mac.h"


/**
 * The MAC module can provide a function for obtaining MAC interface statistics.
 * These include packet counts, raw byte counts and bit errors if the MAC is
 * able to compute them. There are some more advanced statistics which can be
 * available - raw radio channel utilization, number of used TX slots, number
 * of used RX slots, etc.
 */
struct interface_mac_statistics {
	uint32_t rx_bytes; /* counter */
	uint32_t rx_packets; /* counter */
	uint32_t rx_bit_errors; /* counter */
	uint32_t rx_slots; /* gauge */

	uint32_t tx_bytes; /* counter */
	uint32_t tx_packets; /* counter */
	uint32_t tx_slots; /* gauge */

	uint8_t medium_usage; /* gauge, 0-255 = 0%-100% */
	uint32_t slots_total; /* gauge */
};

struct interface_mac_packet_info {
	int32_t fei;
	int16_t rssi;
	int16_t bit_errors;
};

struct interface_mac_vmt {
	int32_t (*send)(void *context, const uint8_t *buf, size_t len);
	int32_t (*receive)(void *context, uint8_t *buf, size_t buflen, size_t *len, struct interface_mac_packet_info *info);
	int32_t (*start)(void *context);
	int32_t (*stop)(void *context);
	int32_t (*get_statistics)(void *context, struct interface_mac_statistics *statistics);
	void *context;
};

struct interface_mac {
	struct hal_interface_descriptor descriptor;
	struct interface_mac_vmt vmt;
};


int32_t interface_mac_init(struct interface_mac *mac);
#define INTERFACE_MAC_INIT_OK 0
#define INTERFACE_MAC_INIT_FAILED -1

int32_t interface_mac_free(struct interface_mac *mac);
#define INTERFACE_MAC_FREE_OK 0
#define INTERFACE_MAC_FREE_FAILED -1

int32_t interface_mac_send(struct interface_mac *mac, const uint8_t *buf, size_t len);
#define INTERFACE_MAC_SEND_OK 0
#define INTERFACE_MAC_SEND_FAILED -1

int32_t interface_mac_receive(struct interface_mac *mac, uint8_t *buf, size_t buflen, size_t *len, struct interface_mac_packet_info *info);
#define INTERFACE_MAC_RECEIVE_OK 0
#define INTERFACE_MAC_RECEIVE_FAILED -1

int32_t interface_mac_start(struct interface_mac *mac);
#define INTERFACE_MAC_START_OK 0
#define INTERFACE_MAC_START_FAILED -1

int32_t interface_mac_stop(struct interface_mac *mac);
#define INTERFACE_MAC_STOP_OK 0
#define INTERFACE_MAC_STOP_FAILED -1

int32_t interface_mac_get_statistics(struct interface_mac *mac, struct interface_mac_statistics *statistics);
#define INTERFACE_MAC_GET_STATISTICS_OK 0
#define INTERFACE_MAC_GET_STATISTICS_FAILED -1


#endif


