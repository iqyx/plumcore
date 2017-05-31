/*
 * File transfer protocol - misc functions
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


static size_t bitmap_compare(const uint8_t *src, const uint8_t *dst, size_t size) {
	size_t bits = 0;
	for (size_t i = 0; i <= size; i++) {
		bits += ((src[i] ^ dst[i]) * 0x200040008001ULL & 0x111111111111111ULL) % 0xf;
	}

	return bits;
}


static size_t bitmap_received(const uint8_t *bitmap, size_t size) {
	size_t bits = 0;
	for (size_t i = 0; i <= size; i++) {
		bits += (bitmap[i] * 0x200040008001ULL & 0x111111111111111ULL) % 0xf;
	}

	return bits;
}


static uint32_t compute_transferred_bytes(FtSession *session) {
	if (session == NULL || session->state == FT_STATE_EMPTY) {
		return 0;
	}

	/* Add complete pieces. */
	uint32_t bytes = session->transferred_pieces * session->piece_size_blocks * session->block_size_bytes;

	/* Add partial pieces (receiver only). */
	if (session->state == FT_STATE_RECEIVING) {
		for (size_t i = 0; i < FT_PIECE_CACHE_COUNT; i++) {
			if (session->pieces[i].used) {
				bytes += bitmap_received(session->pieces[i].bitmap, session->piece_size_blocks / 8) * FT_MAX_BLOCK_SIZE;
			}
		}
	}

	return bytes;
}


static void set_state(FileTransfer *self, FtSession *session, enum ft_state state) {

	if (state != session->state) {
		if (self->file_progress_callback != NULL) {
			self->file_progress_callback(
				session,
				session->callback_context,
				self->file_callback_context
			);
		}
	}

	session->idle_time_ms = 0;
	session->state = state;
}


/** @todo code duplicity */
static void update_progress(FileTransfer *self, FtSession *session, uint32_t time_ms) {
	session->last_progress_ms += time_ms;
	if (session->last_progress_ms >= FT_PROGRESS_UPDATE_INTERVAL_MS) {
		if (self->file_progress_callback != NULL) {
			self->file_progress_callback(
				session,
				session->callback_context,
				self->file_callback_context
			);
		}
		session->last_progress_ms = 0;
	}
}


#define FILE_TRANSFER_SEND_OK 0
#define FILE_TRANSFER_SEND_FAILED -1
static int32_t file_transfer_send(FileTransfer *self, FtSession *session, const FileTransferMessage *msg) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(msg != NULL)) {
		return FILE_TRANSFER_SEND_FAILED;
	}

	if (self->send_callback == NULL) {
		return FILE_TRANSFER_SEND_FAILED;
	}

	if (self->send_callback(msg, session, self->send_context) != FILE_TRANSFER_SEND_CALLBACK_OK) {
		return FILE_TRANSFER_SEND_FAILED;
	}

	return FILE_TRANSFER_SEND_OK;
}


static FtPiece *find_piece(FileTransfer *self, FtSession *session, uint32_t piece_id) {
	(void)self;
	for (size_t i = 0; i < FT_PIECE_CACHE_COUNT; i++) {
		if (session->pieces[i].used && session->pieces[i].id == piece_id) {
			return &(session->pieces[i]);
		}
	}
	return NULL;
}


static FtPiece *read_piece(FileTransfer *self, FtSession *session, uint32_t piece_id) {
	(void)self;
	for (size_t i = 0; i < FT_PIECE_CACHE_COUNT; i++) {
		if (session->pieces[i].used == false) {
			session->pieces[i].used = true;
			session->pieces[i].id = piece_id;
			session->pieces[i].idle_time_ms = 0;
			/** @todo read here */
			//~ u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: reading piece %u"), session, session->pieces[i].id);
			return &(session->pieces[i]);
		}
	}
	return NULL;
}


static void ema_update(uint32_t *ema, uint32_t value) {
	*ema = (*ema * 15 + value) / 16;
}


static bool piece_missing_blocks(FileTransfer *self, FtSession *session, FtPiece *piece) {
	(void)self;
	for (size_t i = 0; i < (session->piece_size_blocks / 8); i++) {
		if (piece->bitmap[i] != 0xff) {
			return true;
		}
	}
	return false;
}


static FtSession *find_session_by_id(FileTransfer *self, const uint8_t *session_id, size_t session_id_size) {
	if (session_id_size > FT_MAX_SESSION_ID_SIZE) {
		session_id_size = FT_MAX_SESSION_ID_SIZE;
	}
	for (size_t i = 0; i < self->session_table_size; i++) {
		if (session_id_size == self->session_table[i].session_id_size &&
		    !memcmp(session_id, self->session_table[i].session_id, session_id_size &&
		    self->session_table[i].state != FT_STATE_EMPTY)) {
			return &(self->session_table[i]);
		}
	}
	return NULL;
}
