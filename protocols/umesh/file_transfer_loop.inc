/*
 * File transfer protocol - main loop
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


/** @todo split into multiple functions */
static void file_transfer_session_step(FileTransfer *self, FtSession *session, uint32_t step_time_ms) {
	(void)step_time_ms;
	if (u_assert(self != NULL) ||
	    u_assert(session != NULL)) {
		return;
	}

	uint32_t blocks_sent = 0;

	session->idle_time_ms += step_time_ms;
	session->message_time_ms += step_time_ms;

	if (session->state == FT_STATE_FILE_METADATA) {
		if (session->idle_time_ms >= FT_SESSION_INIT_TIMEOUT_MS) {
			set_state(self, session, FT_STATE_FAILED);
			return;
		}

		if (session->message_time_ms >= FT_FILE_METADATA_MSG_INTERVAL) {
			send_file_metadata(self, session);
			session->message_time_ms = 0;
			return;
		}
	}

	if (session->state == FT_STATE_FILE_REQUEST) {
		if (session->idle_time_ms >= FT_SESSION_INIT_TIMEOUT_MS) {
			set_state(self, session, FT_STATE_FAILED);
			return;
		}

		if (session->message_time_ms >= FT_FILE_REQUEST_MSG_INTERVAL) {
			send_file_request(self, session);
			session->message_time_ms = 0;
			return;
		}
	}

	if (session->state == FT_STATE_RECEIVING) {
		if (session->idle_time_ms >= FT_SESSION_RUNNING_TIMEOUT_MS) {
			set_state(self, session, FT_STATE_FAILED);
			return;
		}

		size_t used_pieces = 0;

		for (size_t i = 0; i < FT_PIECE_CACHE_COUNT; i++) {
			/* If the piece is used, check if it is completed. */
			if (session->pieces[i].used) {
				if (piece_missing_blocks(self, session, &(session->pieces[i]))) {
					/* If the piece is not completed and enough time passed from the last
					 * block request message, send it again. */
					session->pieces[i].idle_time_ms += step_time_ms;
					if (session->pieces[i].idle_time_ms >= FT_RECEIVER_PIECE_IDLE_MAX) {
						send_block_request(self, session, &(session->pieces[i]));
						session->pieces[i].idle_time_ms = 0;
					}
				} else {
					/*
					 * If the piece is completed, let the peer know, write it to the file
					 * and release it.
					 */
					send_block_request(self, session, &(session->pieces[i]));

					/** @todo write to the file */
					session->transferred_pieces++;
					//~ u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("session %p: writing piece %u"), session, session->pieces[i].id);
					memset(&(session->pieces[i]), 0, sizeof(struct ft_piece));
				}
			}

			/* If an unused piece is available, allocate it for receiving. */
			if (session->pieces[i].used == false) {
				if (session->next_piece_index < ((session->file_size_bytes - 1) / (session->piece_size_blocks * session->block_size_bytes) + 1)) {
					session->pieces[i].used = true;
					session->pieces[i].id = session->next_piece_index;
					session->pieces[i].idle_time_ms = FT_RECEIVER_PIECE_IDLE_MAX;
					memset(&(session->pieces[i].bitmap), 0, FT_MAX_PIECE_BITMAP_SIZE);
					session->next_piece_index++;
				}
			}

			/* Count used pieces. */
			if (session->pieces[i].used) {
				used_pieces++;
			}
		}

		/* stop the file transfer if no pieces are needed. */
		if (used_pieces == 0) {
			set_state(self, session, FT_STATE_FINISHED);
		}

		return;
	}


	if (session->state == FT_STATE_SENDING) {
		if (session->idle_time_ms >= FT_SESSION_RUNNING_TIMEOUT_MS) {
			set_state(self, session, FT_STATE_FAILED);
			return;
		}

		size_t used_pieces = 0;
		for (size_t i = 0; i < FT_PIECE_CACHE_COUNT; i++) {
			/* Send block responses for all requested blocks */
			if (session->pieces[i].used) {
				for (size_t block = 0; block < session->piece_size_blocks; block++) {
					if ((session->pieces[i].bitmap[block / 8] & (0x80 >> (block % 8))) == 0) {
						if (blocks_sent < session->blocks_per_cycle) {
							send_block_response(self, session, &(session->pieces[i]), block);
							session->pieces[i].bitmap[block / 8] |= (0x80 >> (block % 8));
							blocks_sent++;
						}
					}
				}

				session->pieces[i].idle_time_ms += step_time_ms;
				if (session->pieces[i].idle_time_ms > FT_SENDER_PIECE_IDLE_MAX) {
					memset(&(session->pieces[i]), 0, sizeof(struct ft_piece));
				}
			}
			if (session->pieces[i].used) {
				used_pieces++;
			}
		}

		if (used_pieces == 0) {
			set_state(self, session, FT_STATE_FINISHED);
		}

		return;
	}
}


static void file_transfer_main_task(void *p) {
	FileTransfer *self = (FileTransfer *)p;

	while (1) {
		xSemaphoreTake(self->main_task_sem, (TickType_t)1000);
		for (size_t i = 0; i < self->session_table_size; i++) {

			if (self->session_table[i].state != FT_STATE_EMPTY) {
				file_transfer_session_step(self, &(self->session_table[i]), FT_STEP_INTERVAL_MS);
				update_progress(self, &(self->session_table[i]), FT_STEP_INTERVAL_MS);
			}
		}
	}
}


static void file_transfer_timer_callback(TimerHandle_t timer) {
	FileTransfer *self = pvTimerGetTimerID(timer);

	xSemaphoreGive(self->main_task_sem);
}
