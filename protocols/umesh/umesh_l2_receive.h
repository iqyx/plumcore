/**
 * uMeshFw uMesh receive task
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

#ifndef _UMESH_L2_RECEIVE_H_
#define _UMESH_L2_RECEIVE_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "hal_module.h"
#include "interface_mac.h"
#include "module_umesh.h"


void umesh_l2_receive_task(void *p);

/** @todo documentation */
int32_t umesh_l2_receive_check(struct module_umesh *umesh, const uint8_t *data, size_t len, struct interface_mac_packet_info *info);
#define UMESH_L2_RECEIVE_CHECK_OK 0
#define UMESH_L2_RECEIVE_CHECK_FAILED -1

/** @todo documentation */
int32_t umesh_l2_receive(struct module_umesh *umesh, const uint8_t *data, size_t len, struct interface_mac_packet_info *info, uint32_t iface);
#define UMESH_L2_RECEIVE_OK 0
#define UMESH_L2_RECEIVE_FAILED -1

/** @todo documentation */
int32_t umesh_l2_parse_control(const uint8_t **data, uint32_t len, struct umesh_l2_pbuf *pbuf);
#define UMESH_L2_PARSE_CONTROL_OK 0
#define UMESH_L2_PARSE_CONTROL_FAILED -1
#define UMESH_L2_PARSE_CONTROL_UNFINISHED -2
#define UMESH_L2_PARSE_CONTROL_NO_DATA -3

/** @todo documentation */
int32_t umesh_l2_parse_tid(const uint8_t **data, uint32_t len, uint32_t *tid);
#define UMESH_L2_PARSE_TID_OK 0
#define UMESH_L2_PARSE_TID_FAILED -1
#define UMESH_L2_PARSE_TID_NO_DATA -2
#define UMESH_L2_PARSE_TID_TOO_BIG -3

int32_t umesh_l2_parse_counter(const uint8_t **data, uint32_t *len, uint16_t *counter);
#define UMESH_L2_PARSE_COUNTER_OK 0
#define UMESH_L2_PARSE_COUNTER_FAILED -1

/** @todo documentation */
int32_t umesh_l2_parse_data(const uint8_t *header, const uint8_t **data, uint32_t len, struct umesh_l2_pbuf *pbuf, uint8_t *key, uint32_t key_len);
#define UMESH_L2_PARSE_DATA_OK 0
#define UMESH_L2_PARSE_DATA_FAILED -1
#define UMESH_L2_PARSE_DATA_AE_FAILED -2
#define UMESH_L2_PARSE_DATA_NO_DATA -3
#define UMESH_L2_PARSE_DATA_UNSUPPORTED -4
#define UMESH_L2_PARSE_DATA_TOO_BIG -5

/** @todo documentation */
int32_t umesh_l2_receive_route_pbuf(struct module_umesh *umesh, struct umesh_l2_pbuf *pbuf);
#define UMESH_L2_RECEIVE_ROUTE_PBUF_OK 0
#define UMESH_L2_RECEIVE_ROUTE_PBUF_FAILED -1
#define UMESH_L2_RECEIVE_ROUTE_PBUF_UNROUTABLE -2
#define UMESH_L2_RECEIVE_ROUTE_PBUF_BAD_PROTO -3

#endif
