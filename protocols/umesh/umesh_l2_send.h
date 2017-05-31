/**
 * uMeshFw uMesh send
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

#ifndef _UMESH_L2_SEND_H_
#define _UMESH_L2_SEND_H_

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "hal_module.h"
#include "interface_mac.h"
#include "module_umesh.h"


/**
 * @brief Send a previously constructed packet buffer.
 *
 * This function is used by all layer 3 protocol implementations to send a
 * packet to one or more neighbors. The protocol handler must set the packet
 * contents, required security level and the destination. Packet is checked
 * for validity before it is routed and forwarded to the MAC layer.
 *
 * @see umesh_l2_send_check
 *
 * @param umesh The uMesh protocol stack instance used for transmission.
 * @param pbuf The layer 2 packet buffer to send.
 *
 * @return UMESH_L2_SEND_OK
 *             if the packet was successfully routed and send to the MAC layer,
 *         UMESH_L2_SEND_BUSY
 *             if the MAC interface was busy (MAC transmit queue was full),
 *         UMESH_L2_SEND_MALFORMED
 *             if the packet was malformed or some of the required information
 *             was missing,
 *         UMESH_L2_SEND_UNROUTABLE
 *             if the destination was not found in the neighbor table and no
 *             output interface could be selected for transmission or
 *         UMESH_L2_SEND_FAILED
 *             otherwise.
 */
int32_t umesh_l2_send(struct module_umesh *umesh, struct umesh_l2_pbuf *pbuf);
#define UMESH_L2_SEND_OK 0
#define UMESH_L2_SEND_FAILED -1
#define UMESH_L2_SEND_BUSY -2
#define UMESH_L2_SEND_MALFORMED -3
#define UMESH_L2_SEND_UNROUTABLE -4

/**
 * @brief Do a quick check of a packet to be sent.
 *
 * Check if the packet contains all required fliends before it is routed and
 * sent using a selected interface. This check is meant to catch programming
 * errors (checks are done using asserts).
 *
 * Packet buffer members required for sending are:
 *   * either DTID or UMESH_L2_PBUF_FLAG_BROADCAST must be set
 *   * requested security type must be set (or kept at the default value)
 *   * layer 3 protocol must be set
 *   * packet data pointer must be valid (not null)
 *   * packet data length must be set to a value lower than or equal to
 *     UMESH_L2_PACKET_SIZE
 *
 * @param umesh The uMesh protocol stack instance used for transmission.
 * @param pbuf The layer 2 packet buffer to check.
 *
 * @return UMESH_L2_SEND_CHECK_OK if the packet can be sent or
 *         UMESH_L2_SEND_CHECK_FAILED otherwise.
 */
int32_t umesh_l2_send_check(struct module_umesh *umesh, struct umesh_l2_pbuf *pbuf);
#define UMESH_L2_SEND_CHECK_OK 0
#define UMESH_L2_SEND_CHECK_FAILED -1

/**
 * @brief Build the control field part of the layer 2 header.
 *
 * This function uses information from the layer 2 packet buffer @p pbuf and
 * creates a control field part of the packet header. The control field is saved
 * on the position pointed to by @p *data. The function is allowed to write
 * maximum of @p len bytes of data. After writing, @p *data pointer is advanced
 * to point right after the control field.
 *
 * Information from the packet buffer used to construct the control field:
 *   * UMESH_L2_PBUF_FLAG_BROADCAST flag
 *   * STID (Source TID)
 *   * DTID (Destination TID)
 *   * proto (layer 3 protocol)
 *   * sec_algo (error checking/encryption/authentication algorithm)
 *
 * @param pbuf The packet buffer used as a source of data.
 * @param data Pointer to the pointer to the output buffer where the data will
 *             be written.
 * @param len Maximum number of bytes which can be written to the output buffer.
 *
 * @return UMESH_L2_BUILD_CONTROL_OK
 *             if the control field was successfully built,
 *         UMESH_L2_BUILD_CONTROL_BUFFER_SMALL
 *             if the output buffer size was too small to containt the whole
 *             control field (the size is equal to the @p len parameter) or
 *         UMESH_L2_BUILD_CONTROL_FAILED
 *             otherwise.
 */
int32_t umesh_l2_build_control(struct umesh_l2_pbuf *pbuf, uint8_t **data, uint32_t len);
#define UMESH_L2_BUILD_CONTROL_OK 0
#define UMESH_L2_BUILD_CONTROL_FAILED -1
#define UMESH_L2_BUILD_CONTROL_BUFFER_SMALL -2

/** @todo documentation */
int32_t umesh_l2_build_tid(uint32_t tid, uint8_t **data, uint32_t len);
#define UMESH_L2_BUILD_TID_OK 0
#define UMESH_L2_BUILD_TID_FAILED -1
#define UMESH_L2_BUILD_TID_BUFFER_SMALL -2

/** @todo documentation */
int32_t umesh_l2_build_data(struct umesh_l2_pbuf *pbuf, const uint8_t *header, uint8_t **data, uint32_t len, uint8_t *key, uint32_t key_len);
#define UMESH_L2_BUILD_DATA_OK 0
#define UMESH_L2_BUILD_DATA_FAILED -1
#define UMESH_L2_BUILD_DATA_BUFFER_SMALL -2
#define UMESH_L2_BUILD_DATA_UNSUPPORTED -3

/** @todo documentation */
int32_t umesh_l2_send_route_pbuf(struct module_umesh *umesh, struct umesh_l2_pbuf *pbuf);
#define UMESH_L2_SEND_ROUTE_PBUF_OK 0
#define UMESH_L2_SEND_ROUTE_PBUF_FAILED -1
#define UMESH_L2_SEND_ROUTE_PBUF_UNROUTABLE -2


#endif
