/*
 * File transfer protocol
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
#include "timers.h"
#include "queue.h"
#include "task.h"
#include "semphr.h"

#include "interface_rng.h"
#include "file_transfer.pb.h"
#include "file_transfer_config.h"


#define LOCK(x) xSemaphoreTake(x, portMAX_DELAY)
#define RELEASE(x) xSemaphoreGive(x)

enum ft_state {
	/** File transfer session is not active. */
	FT_STATE_EMPTY = 0,
	FT_STATE_PEER,
	FT_STATE_PREPARED,
	FT_STATE_FILE_METADATA,
	FT_STATE_FILE_REQUEST,
	FT_STATE_SENDING,
	FT_STATE_RECEIVING,
	FT_STATE_FINISHED,
	FT_STATE_FAILED
};

typedef struct ft_piece {
	volatile bool used;
	uint8_t bitmap[FT_MAX_PIECE_BITMAP_SIZE];

	/**
	 * Index of the piece within the file. The first piece has index of 0.
	 */
	uint32_t id;

	/**
	 * Idle time of the piece in milliseconds (a time during which no block request or block
	 * response message was received.
	 */
	uint32_t idle_time_ms;

} FtPiece;

typedef struct ft_session_t {
	enum ft_state state;
	SemaphoreHandle_t lock;

	uint8_t session_id[FT_SESSION_ID_SIZE];
	size_t session_id_size;

	/* File properties */
	char file_name[FT_MAX_FILE_NAME_SIZE];
	uint32_t file_size_bytes;
	uint32_t piece_size_blocks;
	uint32_t block_size_bytes;

	uint32_t transferred_pieces;
	FtPiece pieces[FT_PIECE_CACHE_COUNT];
	uint32_t next_piece_index;

	/* Number of bits lost in a piece. */
	uint32_t bits_lost;

	/**
	 * A variable that may be used by the callback functions to store arbitrary data, such as file descriptors.
	 */
	void *callback_context;

	/** @todo block_response_rate_limit */
	uint8_t blocks_per_cycle;

	uint32_t transfer_rate_Bps;
	uint32_t idle_time_ms;
	uint32_t message_time_ms;

	uint32_t last_progress_ms;

} FtSession;

typedef struct file_transfer_t {
	FtSession *session_table;
	size_t session_table_size;
	SemaphoreHandle_t session_table_lock;

	/**
	 * Callbacks for file manipulation.
	 */
	void *file_callback_context;

	int32_t (*file_open_callback)(FtSession *session, void **session_context, void *context, const char *file_name);
	#define FT_FILE_OPEN_CALLBACK_OK 0
	#define FT_FILE_OPEN_CALLBACK_FAILED -1

	uint32_t (*file_get_size_callback)(FtSession *session, void *session_context, void *context);

	int32_t (*file_read_callback)(FtSession *session, void *session_context, uint32_t pos, uint8_t *buf, uint32_t len, void *context);
	#define FT_FILE_READ_CALLBACK_OK 0
	#define FT_FILE_READ_CALLBACK_FAILED -1

	int32_t (*file_write_callback)(FtSession *session, void *session_context, uint32_t pos, const uint8_t *buf, uint32_t len, void *context);
	#define FT_FILE_WRITE_CALLBACK_OK 0
	#define FT_FILE_WRITE_CALLBACK_FAILED -1

	int32_t (*file_close_callback)(FtSession *session, void *session_context, void *context);
	#define FT_FILE_CLOSE_CALLBACK_OK 0
	#define FT_FILE_CLOSE_CALLBACK_FAILED -1

	int32_t (*file_progress_callback)(FtSession *session, void *session_context, void *context);
	#define FT_FILE_PROGRESS_CALLBACK_OK 0
	#define FT_FILE_PROGRESS_CALLBACK_FAILED -1

	int32_t (*send_callback)(const FileTransferMessage *msg, FtSession *session, void *context);
	void *send_context;
	#define FILE_TRANSFER_SEND_CALLBACK_OK 0
	#define FILE_TRANSFER_SEND_CALLBACK_FAILED -1

	struct interface_rng *rng;
	TimerHandle_t main_timer;
	TaskHandle_t main_task;
	SemaphoreHandle_t main_task_sem;

	uint32_t debug;
	#define FT_DEBUG_LOGS (1 << 0)
	#define FT_DEBUG_MESSAGES (1 << 1)

} FileTransfer;


int32_t file_transfer_init(FileTransfer *self, size_t max_sessions);
#define FILE_TRANSFER_INIT_OK 0
#define FILE_TRANSFER_INIT_FAILED -1

int32_t file_transfer_free(FileTransfer *self);
#define FILE_TRANSFER_FREE_OK 0
#define FILE_TRANSFER_FREE_FAILED -1

int32_t file_transfer_set_rng(FileTransfer *self, struct interface_rng *rng);
#define FILE_TRANSFER_SET_RNG_OK 0
#define FILE_TRANSFER_SET_RNG_FAILED -1

FtSession *file_transfer_init_session(FileTransfer *self);
FtSession *file_transfer_init_peer_session(FileTransfer *self, const uint8_t *session_id, size_t session_id_size);

int32_t file_transfer_free_session(FileTransfer *self, FtSession *session);
#define FILE_TRANSFER_FREE_SESSION_OK 0
#define FILE_TRANSFER_FREE_SESSION_FAILED -1

int32_t file_transfer_send_file(FileTransfer *self, FtSession *session, const char *file_name);
#define FILE_TRANSFER_SEND_FILE_OK 0
#define FILE_TRANSFER_SEND_FILE_FAILED -1

int32_t file_transfer_receive_file(FileTransfer *self, FtSession *session, const char *file_name);
#define FILE_TRANSFER_RECEIVE_FILE_OK 0
#define FILE_TRANSFER_RECEIVE_FILE_FAILED -1

int32_t file_transfer_stop(FileTransfer *self, FtSession *session);
#define FILE_TRANSFER_STOP_OK 0
#define FILE_TRANSFER_STOP_FAILED -1

int32_t file_transfer_receive_handler(FileTransfer *self, const FileTransferMessage *msg);
#define FILE_TRANSFER_RECEIVE_HANDLER_OK 0
#define FILE_TRANSFER_RECEIVE_HANDLER_FAILED -1
#define FILE_TRANSFER_RECEIVE_HANDLER_NO_SESSION -2

