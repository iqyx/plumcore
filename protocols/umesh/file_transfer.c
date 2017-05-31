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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "timers.h"
#include "queue.h"
#include "task.h"
#include "semphr.h"

#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include "hal_module.h"
#include "interface_rng.h"

#include "file_transfer.h"
#include "file_transfer.pb.h"
#include "file_transfer_config.h"
#include "pb_decode.h"
#include "pb_encode.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "file-transfer"

/**
 * @todo
 *
 *   - adaptive message sending rate (the rate should be calculated from the block response delivery ratio)
 *   - logging and debug settings
 *   - proper error checking
 *   - piece size calculation
 *   - bitmap size calculation
 *   - transfer rate limiting (+API)
 *   - split file transfer session step
 *   - disable write/read caching, manipulate files directly
 *   - conditional compilation of file request
 */


/* Parts of the fle transfer implementation, static functions. */
#include "file_transfer_misc.inc"
#include "file_transfer_messages.inc"
#include "file_transfer_loop.inc"





int32_t file_transfer_receive_handler(FileTransfer *self, const FileTransferMessage *msg) {
	if (u_assert(self != NULL) ||
	    u_assert(msg != NULL)) {
		return FILE_TRANSFER_RECEIVE_HANDLER_FAILED;
	}

	/* Get the session ID and find the corresponding session context. */
	LOCK(self->session_table_lock);
	FtSession *session = find_session_by_id(self, msg->session_id.bytes, msg->session_id.size);
	if (session == NULL) {
		/* Couldn't match any existing session. */
		RELEASE(self->session_table_lock);
		return FILE_TRANSFER_RECEIVE_HANDLER_NO_SESSION;
	}
	LOCK(session->lock);
	RELEASE(self->session_table_lock);

	switch (msg->which_content) {
		case FileTransferMessage_file_request_tag:
			process_file_request(self, session, &msg->content.file_request);
			break;

		case FileTransferMessage_file_metadata_tag:
			process_file_metadata(self, session, &msg->content.file_metadata);
			break;

		case FileTransferMessage_block_request_tag:
			process_block_request(self, session, &msg->content.block_request);
			break;

		case FileTransferMessage_block_response_tag:
			process_block_response(self, session, &msg->content.block_response);
			break;

		default:
			RELEASE(session->lock);
			return FILE_TRANSFER_RECEIVE_HANDLER_FAILED;
	}

	RELEASE(session->lock);
	return FILE_TRANSFER_RECEIVE_HANDLER_OK;
}


int32_t file_transfer_init(FileTransfer *self, size_t max_sessions) {
	if (u_assert(self != NULL) ||
	    u_assert(max_sessions > 0)) {
		goto err;
	}

	/* Initialize the session table. */
	self->session_table = (FtSession *)malloc(sizeof(FtSession) * max_sessions);
	if (self->session_table == NULL) {
		goto err;
	}
	self->session_table_size = max_sessions;
	memset(self->session_table, 0, sizeof(FtSession) * self->session_table_size);


	/* Initialize the main protocol task and semaphore. */
	self->main_task_sem = xSemaphoreCreateBinary();
	if (self->main_task_sem == NULL) {
		goto err;
	}

	self->session_table_lock = xSemaphoreCreateMutex();
	if (self->session_table_lock == NULL) {
		goto err;
	}

	xTaskCreate(file_transfer_main_task, MODULE_NAME, configMINIMAL_STACK_SIZE + 768, (void *)self, 1, &(self->main_task));
	if (self->main_task == NULL) {
		goto err;
	}

	/* Initialize the main protocol timer. */
	self->main_timer = xTimerCreate("filetransfer-timer", FT_STEP_INTERVAL_MS, pdTRUE, (void *)self, file_transfer_timer_callback);
	if (self->main_timer == NULL) {
		goto err;
	}
	if (xTimerStart(self->main_timer, 10) == pdFAIL) {
		goto err;
	}

	/* Enable/disable debugging features. */
	//~ self->debug = FT_DEBUG_LOGS | FT_DEBUG_MESSAGES;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("protocol initialized"));
	return FILE_TRANSFER_INIT_OK;
err:
	/* Free resources which were already allocated. */
	file_transfer_free(self);
	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("protocol initialization failed"));
	return FILE_TRANSFER_INIT_FAILED;
}


int32_t file_transfer_free(FileTransfer *self) {
	if (u_assert(self != NULL)) {
		return FILE_TRANSFER_FREE_FAILED;
	}

	if (self->session_table != NULL) {
		free(self->session_table);
	}

	if (self->main_timer != NULL) {
		xTimerDelete(self->main_timer, 10);
	}

	if (self->main_task != NULL) {
		vTaskDelete(self->main_task);
	}

	if (self->main_task_sem != NULL) {
		vSemaphoreDelete(self->main_task_sem);
	}

	if (self->session_table_lock != NULL) {
		vSemaphoreDelete(self->session_table_lock);
	}

	return FILE_TRANSFER_FREE_OK;
}


int32_t file_transfer_set_rng(FileTransfer *self, struct interface_rng *rng) {
	if (u_assert(self != NULL) ||
	    u_assert(rng != NULL)) {
		return FILE_TRANSFER_SET_RNG_FAILED;
	}

	self->rng = rng;

	return FILE_TRANSFER_SET_RNG_OK;
}


FtSession *file_transfer_init_session(FileTransfer *self) {
	if (u_assert(self != NULL)) {
		return NULL;
	}

	LOCK(self->session_table_lock);
	FtSession *session = NULL;
	for (size_t i = 0; i < self->session_table_size; i++) {
		if (self->session_table[i].state == FT_STATE_EMPTY) {
			session = &(self->session_table[i]);
		}
	}

	session->lock = xSemaphoreCreateMutex();
	if (session->lock == NULL) {
		goto err;
	}

	/** @todo */
	//~ interface_rng_read(self->rng, session->session_id, FT_SESSION_ID_SIZE);
	memcpy(session->session_id, &(uint8_t[]){1, 2}, 2);
	session->session_id_size = 2;

	session->blocks_per_cycle = 5;
	set_state(self, session, FT_STATE_PREPARED);

	RELEASE(self->session_table_lock);
	return session;

err:
	RELEASE(self->session_table_lock);
	return NULL;
}


/** @todo remove code duplicity */
FtSession *file_transfer_init_peer_session(FileTransfer *self, const uint8_t *session_id, size_t session_id_size) {
	if (u_assert(self != NULL)) {
		return NULL;
	}

	LOCK(self->session_table_lock);
	FtSession *session = NULL;
	for (size_t i = 0; i < self->session_table_size; i++) {
		if (self->session_table[i].state == FT_STATE_EMPTY) {
			session = &(self->session_table[i]);
		}
	}

	session->lock = xSemaphoreCreateMutex();
	if (session->lock == NULL) {
		goto err;
	}

	if (session_id_size > FT_MAX_SESSION_ID_SIZE) {
		session_id_size = FT_MAX_SESSION_ID_SIZE;
	}
	memcpy(session->session_id, session_id, session_id_size);
	session->session_id_size = session_id_size;

	session->blocks_per_cycle = 5;
	set_state(self, session, FT_STATE_PEER);

	RELEASE(self->session_table_lock);
	return session;

err:
	RELEASE(self->session_table_lock);
	return NULL;
}



int32_t file_transfer_free_session(FileTransfer *self, FtSession *session) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(session->state == FT_STATE_FINISHED || session->state == FT_STATE_FAILED)) {
		return FILE_TRANSFER_FREE_FAILED;
	}

	if (session->lock != NULL) {
		vSemaphoreDelete(session->lock);
	}
	memset(session, 0, sizeof(FtSession));

	return FILE_TRANSFER_FREE_OK;
}


int32_t file_transfer_send_file(FileTransfer *self, FtSession *session, const char *file_name) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(session->state == FT_STATE_PREPARED)) {
		return FILE_TRANSFER_SEND_FILE_FAILED;
	}

	LOCK(session->lock);

	/** @todo save file name, open file, read file properties */
	size_t fnsize = strlen(file_name);
	if (fnsize >= FT_MAX_FILE_NAME_SIZE) {
		fnsize = FT_MAX_FILE_NAME_SIZE - 1;
	}
	memcpy(session->file_name, file_name, fnsize);
	session->file_name[fnsize] = '\0';

	/** @todo open file, read file properties */
	session->file_size_bytes = 50000;
	session->piece_size_blocks = 32;
	session->block_size_bytes = 32;

	set_state(self, session, FT_STATE_FILE_METADATA);
	RELEASE(session->lock);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("starting transfer of file '%s'"), file_name);
	return FILE_TRANSFER_SEND_FILE_OK;
}


int32_t file_transfer_receive_file(FileTransfer *self, FtSession *session, const char *file_name) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(session->state == FT_STATE_PREPARED)) {
		return FILE_TRANSFER_RECEIVE_FILE_FAILED;
	}

	LOCK(session->lock);

	size_t fnsize = strlen(file_name);
	if (fnsize >= FT_MAX_FILE_NAME_SIZE) {
		fnsize = FT_MAX_FILE_NAME_SIZE - 1;
	}
	memcpy(session->file_name, file_name, fnsize);
	session->file_name[fnsize] = '\0';

	set_state(self, session, FT_STATE_FILE_REQUEST);
	RELEASE(session->lock);

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("starting transfer of file '%s'"), file_name);
	return FILE_TRANSFER_SEND_FILE_OK;
}


int32_t file_transfer_stop(FileTransfer *self, FtSession *session) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(session->state == FT_STATE_RECEIVING || session->state == FT_STATE_SENDING)) {
		return FILE_TRANSFER_STOP_FAILED;
	}

	LOCK(session->lock);
	/** @todo close file here */
	set_state(self, session, FT_STATE_FINISHED);
	RELEASE(session->lock);

	return FILE_TRANSFER_STOP_OK;
}
