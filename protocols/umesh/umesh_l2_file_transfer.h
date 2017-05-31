/*
 * Layer 2 file transfer protocol wrapper
 *
 * Copyright (C) 2016, Marek Koza, qyx@krtko.org
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
#include <string.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "file_transfer.h"
#include "umesh_l3_protocol.h"


typedef struct umesh_l2_file_transfer_session {
	uint32_t peer_tid;
	FtSession *file_transfer_session;
	volatile bool used;

} L2FtSession;

typedef struct umesh_l2_file_transfer {
	L2FtSession *session_table;
	uint32_t session_table_size;

	struct interface_rng *rng;
	struct umesh_l3_protocol protocol;
	struct umesh_l2_pbuf pbuf;
	FileTransfer file_transfer;
} L2FileTransfer;


int32_t umesh_l2_file_transfer_init(L2FileTransfer *self, uint32_t table_size);
#define UMESH_L2_FILE_TRANSFER_INIT_OK 0
#define UMESH_L2_FILE_TRANSFER_INIT_FAILED -1
#define UMESH_L2_FILE_TRANSFER_INIT_NOMEM -2

int32_t umesh_l2_file_transfer_free(L2FileTransfer *self);
#define UMESH_L2_FILE_TRANSFER_FREE_OK 0
#define UMESH_L2_FILE_TRANSFER_FREE_FAILED -1

int32_t umesh_l2_file_transfer_set_rng(L2FileTransfer *self, struct interface_rng *rng);
#define UMESH_L2_FILE_TRANSFER_SET_RNG_OK 0
#define UMESH_L2_FILE_TRANSFER_SET_RNG_FAILED -1

int32_t umesh_l2_file_transfer_send(L2FileTransfer *self, const char *file_name, uint32_t peer_tid);
#define UMESH_L2_FILE_TRANSFER_SEND_OK 0
#define UMESH_L2_FILE_TRANSFER_SEND_FAILED -1

int32_t umesh_l2_file_transfer_receive(L2FileTransfer *self, const char *file_name, uint32_t peer_tid);
#define UMESH_L2_FILE_TRANSFER_RECEIVE_OK 0
#define UMESH_L2_FILE_TRANSFER_RECEIVE_FAILED -1

