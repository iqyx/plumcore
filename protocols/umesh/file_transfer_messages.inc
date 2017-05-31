/*
 * File transfer protocol - message building and processing
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


static void send_file_metadata(FileTransfer *self, FtSession *session) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(session->state == FT_STATE_FILE_METADATA)) {
		return;
	}

	LOCK(session->lock);
	if (self->debug & FT_DEBUG_MESSAGES) {
		u_log(
			system_log,
			LOG_TYPE_DEBUG,
			U_LOG_MODULE_PREFIX("session %p: sending file metadata piece_size=%u block_size=%u file_size=%u file_name='%s'"),
			session,
			session->piece_size_blocks,
			session->block_size_bytes,
			session->file_size_bytes,
			session->file_name
		);
	}

	FileTransferMessage msg = FileTransferMessage_init_zero;
	memcpy(&msg.session_id.bytes, session->session_id, session->session_id_size);
	msg.session_id.size = session->session_id_size;

	msg.which_content = FileTransferMessage_file_metadata_tag;
	msg.content.file_metadata.piece_size_blocks = 32;
	msg.content.file_metadata.block_size_bytes = 32;
	msg.content.file_metadata.file_size_bytes = session->file_size_bytes;

	memcpy(&msg.content.file_metadata.file_name.bytes, session->file_name, strlen(session->file_name));
	msg.content.file_metadata.file_name.size = strlen(session->file_name);
	msg.content.file_metadata.has_file_name = true;

	/** @todo set check field (sha256 or crc32) */
	RELEASE(session->lock);

	/* Ignore errors. */
	file_transfer_send(self, session, &msg);
}


static void send_file_request(FileTransfer *self, FtSession *session) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(session->state == FT_STATE_FILE_REQUEST)) {
		return;
	}

	LOCK(session->lock);
	if (self->debug & FT_DEBUG_MESSAGES) {
		u_log(
			system_log,
			LOG_TYPE_DEBUG,
			U_LOG_MODULE_PREFIX("session %p: sending file request file_name='%s'"),
			session,
			session->file_name
		);
	}

	FileTransferMessage msg = FileTransferMessage_init_zero;
	memcpy(&msg.session_id.bytes, session->session_id, session->session_id_size);
	msg.session_id.size = session->session_id_size;

	msg.which_content = FileTransferMessage_file_request_tag;

	memcpy(&msg.content.file_request.file_name.bytes, session->file_name, strlen(session->file_name));
	msg.content.file_request.file_name.size = strlen(session->file_name);

	RELEASE(session->lock);

	/* Ignore errors. */
	file_transfer_send(self, session, &msg);
}


static void send_block_request(FileTransfer *self, FtSession *session, FtPiece *piece) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(piece != NULL) ||
	    u_assert(session->state == FT_STATE_RECEIVING) ||
	    u_assert(piece->used)) {
		return;
	}

	LOCK(session->lock);

	if (self->debug & FT_DEBUG_MESSAGES) {
		u_log(
			system_log,
			LOG_TYPE_DEBUG,
			U_LOG_MODULE_PREFIX("session %p: sending block request piece=%u"),
			session,
			piece->id
		);
	}

	FileTransferMessage msg = FileTransferMessage_init_zero;
	memcpy(&msg.session_id.bytes, session->session_id, session->session_id_size);
	msg.session_id.size = session->session_id_size;

	msg.which_content = FileTransferMessage_block_request_tag;
	msg.content.block_request.piece = piece->id;
	msg.content.block_request.transferred_pieces = session->transferred_pieces;

	if (piece_missing_blocks(self, session, piece)) {
		memcpy(&msg.content.block_request.bitmap.bytes, piece->bitmap, session->piece_size_blocks / 8);
		msg.content.block_request.bitmap.size = session->piece_size_blocks / 8;
	} else {
		msg.content.block_request.bitmap.size = 0;
	}

	RELEASE(session->lock);

	file_transfer_send(self, session, &msg);
}


static void send_block_response(FileTransfer *self, FtSession *session, struct ft_piece *piece, uint32_t block) {
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL) ||
	    u_assert(piece != NULL) ||
	    u_assert(session->state == FT_STATE_SENDING) ||
	    u_assert(piece->used)) {
		return;
	}

	LOCK(session->lock);

	if (self->debug & FT_DEBUG_MESSAGES) {
		u_log(
			system_log,
			LOG_TYPE_DEBUG,
			U_LOG_MODULE_PREFIX("session %p: sending block response piece=%u block=%u"),
			session,
			piece->id,
			block
		);
	}

	FileTransferMessage msg = FileTransferMessage_init_zero;
	memcpy(&msg.session_id.bytes, session->session_id, session->session_id_size);
	msg.session_id.size = session->session_id_size;

	msg.which_content = FileTransferMessage_block_response_tag;

	msg.content.block_response.piece = piece->id;
	msg.content.block_response.block = block;
	msg.content.block_response.data.size = session->block_size_bytes;

	RELEASE(session->lock);

	file_transfer_send(self, session, &msg);
}


static void process_file_request(FileTransfer *self, FtSession *session, const FileRequest *msg) {
	if (self->debug & FT_DEBUG_MESSAGES) {
		/** @todo print file name */
		u_log(
			system_log,
			LOG_TYPE_DEBUG,
			U_LOG_MODULE_PREFIX("session %p: file request received"),
			session
		);
	}

	/* FYI: session is locked by the caller. */
	if (session->state == FT_STATE_PEER) {

		size_t fnsize = msg->file_name.size;
		if (fnsize >= FT_MAX_FILE_NAME_SIZE) {
			fnsize = FT_MAX_FILE_NAME_SIZE - 1;
		}
		memcpy(session->file_name, &msg->file_name.bytes, fnsize);
		session->file_name[fnsize] = '\0';


		/** @todo open file, read file properties */
		session->file_size_bytes = 50000;
		session->piece_size_blocks = 32;
		session->block_size_bytes = 32;

		set_state(self, session, FT_STATE_FILE_METADATA);
		return;
	}

	/* Unexpected message */
	u_assert(false);
}


static void process_file_metadata(FileTransfer *self, FtSession *session, const FileMetadata *msg) {
	if (self->debug & FT_DEBUG_MESSAGES) {
		u_log(
			system_log,
			LOG_TYPE_DEBUG,
			U_LOG_MODULE_PREFIX("session %p: file metadata received file_size=%u piece_size=%u"),
			session,
			msg->file_size_bytes,
			msg->piece_size_blocks
		);
	}

	/* FYI: session is locked by the caller. */
	if (session->state == FT_STATE_FILE_REQUEST) {
		/* File name is already known, there should be no file_name in the metadata message. */
		session->file_size_bytes = msg->file_size_bytes;
		session->piece_size_blocks = msg->piece_size_blocks;
		session->block_size_bytes = msg->block_size_bytes;

		/** @todo create file */

		set_state(self, session, FT_STATE_RECEIVING);
		return;
	}

	if (session->state == FT_STATE_PEER) {
		size_t fnsize = msg->file_name.size;
		if (fnsize >= FT_MAX_FILE_NAME_SIZE) {
			fnsize = FT_MAX_FILE_NAME_SIZE - 1;
		}
		memcpy(session->file_name, &msg->file_name.bytes, fnsize);
		session->file_name[fnsize] = '\0';

		session->file_size_bytes = msg->file_size_bytes;
		session->piece_size_blocks = msg->piece_size_blocks;
		session->block_size_bytes = msg->block_size_bytes;

		/** @todo create file */

		set_state(self, session, FT_STATE_RECEIVING);
		return;
	}

	/* Unexpected message */
	/** @todo remove the assert */
	u_assert(false);
}


static void process_block_request(FileTransfer *self, FtSession *session, const BlockRequest *msg) {
	if (self->debug & FT_DEBUG_MESSAGES) {
		u_log(
			system_log,
			LOG_TYPE_DEBUG,
			U_LOG_MODULE_PREFIX("session %p: block request received piece=%u"),
			session,
			msg->piece
		);
	}

	/* FYI: session is locked by the caller. */
	if (session->state == FT_STATE_FILE_METADATA) {
		/*
		 * When the peer responds with a block request message, it is known that the FileMetadata
		 * message was received successfully. Move to the FT_STATE_SENDING state.
		 */
		set_state(self, session, FT_STATE_SENDING);
	}

	if (session->state == FT_STATE_SENDING) {
		/*
		 * During sending, the peer continuously sends block request messages. They contain information
		 * about the count of successfully transferred pieces. Save this information to use it for progress
		 * estimation later.
		 */
		session->transferred_pieces = msg->transferred_pieces;

		FtPiece *piece = find_piece(self, session, msg->piece);
		if (piece == NULL) {
			/* If the piece is not found in the cache, try to read it from the file. */
			piece = read_piece(self, session, msg->piece);
		}

		/*
		 * If the piece was found and the bitmap in the received message is empty, it means that the piece
		 * is no longer needed. Release it.
		 */
		if (piece != NULL && msg->bitmap.size == 0) {
			piece->idle_time_ms = FT_SENDER_PIECE_IDLE_MAX;
			//~ u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: freeing piece %u"), session, msg->piece);
			return;
		}
		if (piece == NULL) {
			/* If the piece cannot be allocated or read, do not process the block request message. */
			//~ u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: cannot allocate piece %u"), session, msg->piece);
			return;
		}

		/* Piece was found or allocated and read, reset its idle time now. */
		piece->idle_time_ms = 0;

		if ((msg->bitmap.size * 8) != session->piece_size_blocks) {
			//~ u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: invalid bitmap size"), session);
			return;
		}

		/*
		 * The bitmap has some bits set (which correspond to the blocks sent). If the blocks are received correctly,
		 * there should be the same bits set in the new request. If they are not, some blocks were lost during
		 * transmission. We are monitoring this value to adjust the sending rate.
		 */
		ema_update(&session->bits_lost, bitmap_compare((uint8_t *)&(piece->bitmap), msg->bitmap.bytes, session->piece_size_blocks / 8) * session->piece_size_blocks);
		//~ u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: lost bits EMA = %u"), session, session->bits_lost);

		/* Update the bitmap to send requested blocks in the next cycle. */
		memcpy(&(piece->bitmap), msg->bitmap.bytes, session->piece_size_blocks / 8);

		/** @todo if the bitmap is full (all pieces received), free the piece from the cache */

		/* Stay in the FT_STATE_SENDING state. */
		set_state(self, session, FT_STATE_SENDING);
		return;
	}

	/* Unexpected message */
	/** @todo remove the assert */
	u_assert(false);
}

/** @todo */
static void process_block_response(FileTransfer *self, FtSession *session, const BlockResponse *msg) {
	if (self->debug & FT_DEBUG_MESSAGES) {
		u_log(
			system_log,
			LOG_TYPE_DEBUG,
			U_LOG_MODULE_PREFIX("session %p: block response received piece=%u block=%u data_size=%u"),
			session,
			msg->piece,
			msg->block,
			msg->data.size
		);
	}

	/* FYI: session is locked by the caller. */
	if (session->state == FT_STATE_RECEIVING) {
		FtPiece *piece = find_piece(self, session, msg->piece);
		if (piece == NULL) {
			/* Piece not found, ignore the response. */
			return;
		}

		/* The block index is higher than the count of blocks in a piece. */
		if (msg->block >= session->piece_size_blocks) {
			return;
		}

		/* Mark the block as received. */
		piece->bitmap[msg->block / 8] |= (0x80 >> (msg->block % 8));

		/** @todo save the block */

		/* Stay in the receiving state. */
		set_state(self, session, FT_STATE_RECEIVING);
		session->idle_time_ms = 0;
		return;
	}

	/* Unexpected message */
	/** @todo remove the assert */
	u_assert(false);
}

