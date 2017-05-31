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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "u_assert.h"
#include "u_log.h"
#include "port.h"

#include "hal_module.h"
#include "interface_rng.h"

#include "umesh_l2_file_transfer.h"
#include "file_transfer.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include "umesh_l2_send.h"
#include "umesh_l2_pbuf.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "l2-file-transfer"


static L2FtSession *allocate_session(L2FileTransfer *self) {
	if (u_assert(self != NULL) ||
	    u_assert(self->session_table != NULL) ||
	    u_assert(self->session_table_size > 0)) {
		return NULL;
	}

	for (size_t i = 0; i < self->session_table_size; i++) {
		if (self->session_table[i].used == false) {
			return &(self->session_table[i]);
		}
	}

	return NULL;
}


static int32_t umesh_l2_file_transfer_receive_handler(struct umesh_l2_pbuf *pbuf, void *context) {
	if (u_assert(pbuf != NULL) ||
	    u_assert(context != NULL)) {
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}
	L2FileTransfer *self = (L2FileTransfer *)context;

	pb_istream_t stream;
        stream = pb_istream_from_buffer(pbuf->data, pbuf->len);

	FileTransferMessage msg;
	if (!pb_decode(&stream, FileTransferMessage_fields, &msg))
	{
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}


	int32_t res = file_transfer_receive_handler(&self->file_transfer, &msg);
	if (res == FILE_TRANSFER_RECEIVE_HANDLER_NO_SESSION) {

		L2FtSession *session = allocate_session(self);
		if (session == NULL) {
			return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
		}

		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("remote file transfer request from %08x"), pbuf->stid);
		session->peer_tid = pbuf->stid;

		/* Initiate a new file transfer session with unknown file name and role. */
		FtSession *ft_session = file_transfer_init_peer_session(
			&self->file_transfer,
			msg.session_id.bytes,
			msg.session_id.size
		);

		if (ft_session == NULL) {
			return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
		}

		session->used = true;
		session->file_transfer_session = ft_session;

		/* And process the message again. */
		res = file_transfer_receive_handler(&self->file_transfer, &msg);

	}
	if (res != FILE_TRANSFER_RECEIVE_HANDLER_OK) {
		return UMESH_L3_PROTOCOL_RECEIVE_FAILED;
	}

	return UMESH_L3_PROTOCOL_RECEIVE_OK;
}


/**********************************************************************************************************************
 * File transfer protocol callbacks
 **********************************************************************************************************************/

static int32_t file_open_callback(FtSession *session, void **session_context, void *context, const char *file_name) {
	(void)session;
	(void)session_context;
	(void)context;
	(void)file_name;

	return 0;
}


static uint32_t file_get_size_callback(FtSession *session, void *session_context, void *context) {
	(void)session;
	(void)session_context;
	(void)context;
	return 50000;
}


static int32_t file_read_callback(FtSession *session, void *session_context, uint32_t pos, uint8_t *buf, uint32_t len, void *context) {
	(void)session;
	(void)session_context;
	(void)pos;
	(void)buf;
	(void)len;
	(void)context;
	return 0;
}


static int32_t file_write_callback(FtSession *session, void *session_context, uint32_t pos, const uint8_t *buf, uint32_t len, void *context) {
	(void)session;
	(void)session_context;
	(void)pos;
	(void)buf;
	(void)len;
	(void)context;
	return 0;
}


static int32_t file_close_callback(FtSession *session, void *session_context, void *context) {
	(void)session;
	(void)session_context;
	(void)context;
	return 0;
}


static int32_t file_progress_callback(FtSession *session, void *session_context, void *context) {
	(void)session;
	(void)session_context;

	L2FileTransfer *self = (L2FileTransfer *)context;

	if (session->state == FT_STATE_FINISHED || session->state == FT_STATE_FAILED) {
		u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("stopping session"));
		file_transfer_free_session(&self->file_transfer, session);

		for (size_t i = 0; i < self->session_table_size; i++) {
			if (self->session_table[i].file_transfer_session == session) {
				memset(&(self->session_table[i]), 0, sizeof(L2FtSession));
			}
		}
	}
	return 0;
}


static int32_t umesh_l2_file_transfer_send_callback(const FileTransferMessage *msg, FtSession *session, void *context) {

	L2FileTransfer *self = (L2FileTransfer *)context;

	/* Find the corresponding session. */
	L2FtSession *ft_session = NULL;
	for (size_t i = 0; i < self->session_table_size; i++) {
		if (self->session_table[i].file_transfer_session == session) {
			ft_session = &(self->session_table[i]);
		}
	}
	if (ft_session == NULL) {
		return FILE_TRANSFER_SEND_CALLBACK_FAILED;
	}

	umesh_l2_pbuf_clear(&(self->pbuf));

	self->pbuf.dtid = ft_session->peer_tid;
	self->pbuf.sec_type = UMESH_L2_PBUF_SECURITY_VERIFY;
	self->pbuf.proto = UMESH_L3_PROTOCOL_FILE_TRANSFER;

	pb_ostream_t stream;
        stream = pb_ostream_from_buffer(self->pbuf.data, self->pbuf.size);

        if (!pb_encode(&stream, FileTransferMessage_fields, msg)) {
		return FILE_TRANSFER_SEND_CALLBACK_FAILED;
	}
	self->pbuf.len = stream.bytes_written;

	/** @todo Layer 2 send should be a dependency. This is wrong. */
	if (umesh_l2_send(&umesh, &(self->pbuf)) != UMESH_L2_SEND_OK) {
		return FILE_TRANSFER_SEND_CALLBACK_FAILED;
	}

	return FILE_TRANSFER_SEND_CALLBACK_OK;
}


/**********************************************************************************************************************
 * API
 **********************************************************************************************************************/

int32_t umesh_l2_file_transfer_init(L2FileTransfer *self, uint32_t table_size) {
	if (u_assert(self != NULL) ||
	    u_assert(table_size > 0)) {
		return UMESH_L2_FILE_TRANSFER_INIT_FAILED;
	}

	/* Initialize the session table. */
	self->session_table = (L2FtSession *)malloc(sizeof(L2FtSession) * table_size);
	if (self->session_table == NULL) {
		return UMESH_L2_FILE_TRANSFER_INIT_NOMEM;
	}
	self->session_table_size = table_size;

	/* Initialze layer 3 protocol descriptor and set the receive handler. */
	self->protocol.name = "l2filetransfer";
	self->protocol.context = (void *)self;
	self->protocol.receive_handler = umesh_l2_file_transfer_receive_handler;

	memset(self->session_table, 0, sizeof(L2FtSession) * self->session_table_size);

	/* Initialize a single shared L2 packet buffer used to send protocol data. */
	umesh_l2_pbuf_init(&(self->pbuf));

	/* Initialize used protocols. */
	/** @todo make max_sessions configurable */
	if (file_transfer_init(&self->file_transfer, 1) != FILE_TRANSFER_INIT_OK) {
		return UMESH_L2_FILE_TRANSFER_INIT_FAILED;
	}

	/* Set protocol callbacks. */
	self->file_transfer.file_callback_context = (void *)self;
	self->file_transfer.file_open_callback = file_open_callback;
	self->file_transfer.file_get_size_callback = file_get_size_callback;
	self->file_transfer.file_read_callback = file_read_callback;
	self->file_transfer.file_write_callback = file_write_callback;
	self->file_transfer.file_close_callback = file_close_callback;
	self->file_transfer.file_progress_callback = file_progress_callback;

	self->file_transfer.send_callback = umesh_l2_file_transfer_send_callback;
	self->file_transfer.send_context = (void *)self;

	/** @todo */

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("module initialized"));
	return UMESH_L2_FILE_TRANSFER_INIT_OK;
}


int32_t umesh_l2_file_transfer_free(L2FileTransfer *self) {
	if (u_assert(self != NULL)) {
		return UMESH_L2_FILE_TRANSFER_FREE_FAILED;
	}

	if (self->session_table != NULL) {
		free(self->session_table);
	}

	return UMESH_L2_FILE_TRANSFER_FREE_OK;
}


int32_t umesh_l2_file_transfer_set_rng(L2FileTransfer *self, struct interface_rng *rng) {
	if (u_assert(self != NULL) ||
	    u_assert(rng != NULL)) {
		return UMESH_L2_FILE_TRANSFER_SET_RNG_FAILED;
	}

	self->rng = rng;
	file_transfer_set_rng(&self->file_transfer, rng);

	return UMESH_L2_FILE_TRANSFER_SET_RNG_OK;
}


int32_t umesh_l2_file_transfer_send(L2FileTransfer *self, const char *file_name, uint32_t peer_tid) {

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("send file '%s' to %08x"), file_name, peer_tid);

	L2FtSession *session = allocate_session(self);
	if (session == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate new L2 file transfer session"));
		return UMESH_L2_FILE_TRANSFER_SEND_FAILED;
	}

	FtSession *ft_session = file_transfer_init_session(&self->file_transfer);

	if (ft_session == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start file transfer protocol session"));
		return UMESH_L2_FILE_TRANSFER_SEND_FAILED;
	}


	if (file_transfer_send_file(&self->file_transfer, ft_session, file_name) != FILE_TRANSFER_SEND_FILE_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the file transfer"));
		return UMESH_L2_FILE_TRANSFER_SEND_FAILED;
	}

	session->peer_tid = peer_tid;
	session->used = true;
	session->file_transfer_session = ft_session;

	return UMESH_L2_FILE_TRANSFER_SEND_OK;
}


/** @todo code duplicity */
int32_t umesh_l2_file_transfer_receive(L2FileTransfer *self, const char *file_name, uint32_t peer_tid) {

	(void)self;
	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("receive file '%s' from %08x"), file_name, peer_tid);

	L2FtSession *session = allocate_session(self);
	if (session == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot allocate new L2 file transfer session"));
		return UMESH_L2_FILE_TRANSFER_RECEIVE_FAILED;
	}

	FtSession *ft_session = file_transfer_init_session(&self->file_transfer);

	if (ft_session == NULL) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start file transfer protocol session"));
		return UMESH_L2_FILE_TRANSFER_RECEIVE_FAILED;
	}

	if (file_transfer_receive_file(&self->file_transfer, ft_session, file_name) != FILE_TRANSFER_RECEIVE_FILE_OK) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("cannot start the file transfer"));
		return UMESH_L2_FILE_TRANSFER_RECEIVE_FAILED;
	}

	session->peer_tid = peer_tid;
	session->used = true;
	session->file_transfer_session = ft_session;

	return UMESH_L2_FILE_TRANSFER_RECEIVE_OK;
}
