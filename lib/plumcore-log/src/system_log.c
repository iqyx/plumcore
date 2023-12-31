/**
 * system logging service/API
 *
 * Copyright (C) 2014, Marek Koza, qyx@krtko.org
 *
 * This file is part of uMesh node firmware (http://qyx.krtko.org/embedded/umesh)
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
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "system_log.h"


int32_t log_cbuffer_init(struct log_cbuffer *buf, uint8_t *data, uint32_t size) {
	buf->size = size;
	buf->d = data;

	buf->blk_first = 0;
	buf->blk_last = 0;
	#if LOG_CBUFFER_APPEND_LOCK == true
		chMtxInit(&(buf->add_mutex));
	#endif

	buf->print_handler = NULL;
	buf->print_handler_ctx = NULL;
	buf->time_handler = NULL;
	buf->time_handler_ctx = NULL;

	uint32_t pos = 0;

	log_cbuffer_set_header(buf, pos, LOG_TYPE_INFO);
	log_cbuffer_set_time(buf, pos, 0);
	log_cbuffer_set_message(buf, pos, "clog initialized", 16);

	buf->blk_last = pos;

	return LOG_CBUFFER_INIT_OK;
}


int32_t log_cbuffer_set_header(struct log_cbuffer *buf, uint32_t pos, uint8_t header) {
	buf->d[pos] = header;

	return LOG_CBUFFER_SET_HEADER_OK;
}


int32_t log_cbuffer_set_time(struct log_cbuffer *buf, uint32_t pos, uint32_t time) {
	buf->d[pos + 4] = (time >> 24) & 0xff;
	buf->d[pos + 5] = (time >> 16) & 0xff;
	buf->d[pos + 6] = (time >> 8) & 0xff;
	buf->d[pos + 7] = (time) & 0xff;

	return LOG_CBUFFER_SET_TIME_OK;
}


int32_t log_cbuffer_set_message(struct log_cbuffer *buf, uint32_t pos, char *message, uint32_t len) {

	/* save length to header at position 2, see header structure in .h file */
	buf->d[pos + 2] = ((len + 1) >> 8) & 0xff;
	buf->d[pos + 3] = (len + 1) & 0xff;


	/* and finally copy the message */
	strncpy((char *)(&(buf->d[8 + pos])), message, len + 1);
	buf->d[8 + pos + len] = 0;

	return LOG_CBUFFER_SET_MSG_OK;
}


int32_t log_cbuffer_print(struct log_cbuffer *buf) {
	uint32_t pos = buf->blk_first;

	/* traverse all blocks in the buffer and print their contents */
	if (buf->print_handler != NULL) {
		buf->print_handler(buf, pos, buf->print_handler_ctx);
	}
	while (pos != buf->blk_last) {
		log_cbuffer_next_block(buf, &pos);
		if (buf->print_handler != NULL) {
			buf->print_handler(buf, pos, buf->print_handler_ctx);
		}
	}

	return LOG_CBUFFER_PRINT_OK;
}


int32_t log_cbuffer_next_block(struct log_cbuffer *buf, uint32_t *pos) {

	uint32_t length = (buf->d[*pos + 2] << 8) | buf->d[*pos + 3];

	if (buf->d[*pos] & LOG_HEADER_OVR) {
		/* if the current block has overflow flag set, new block is always at position 0 */
		*pos = 0;
	} else {
		/* if not, new position is current position incremented by header size and
		 * current block length */
		*pos = *pos + 8 + length;
	}

	return LOG_CBUFFER_NEXT_BLK_OK;
}


int32_t log_cbuffer_append_msg(struct log_cbuffer *buf, char *msg, uint8_t type) {

	/* TODO: replace with abstracted API */
	#if LOG_CBUFFER_APPEND_LOCK == true
		chMtxLock(&(buf->add_mutex));
	#endif

	/* compute new block position */
	uint32_t new_pos = buf->blk_last;
	log_cbuffer_next_block(buf, &new_pos);

	uint32_t msg_len = strlen(msg);
	if (msg_len > LOG_CBUFFER_MSG_LEN_MAX) {
		msg_len = LOG_CBUFFER_MSG_LEN_MAX;
	}

	/* and compute required block length */
	uint32_t length = msg_len + 1 + 8;

	/* can we place new block at current position? */
	int place_ok = 0;

	while (1) {
		/* if conditions are met, place buffer here */
		if (place_ok) {
			//~ printf("append: place buffer at %d\n", new_pos);
			log_cbuffer_set_header(buf, new_pos, type);

			/* TODO: set time */
			uint32_t time = 0;
			if (buf->time_handler != NULL) {
				buf->time_handler(buf, &time, buf->time_handler_ctx);
			}
			log_cbuffer_set_time(buf, new_pos, time);
			log_cbuffer_set_message(buf, new_pos, msg, msg_len);

			buf->blk_last = new_pos;

			if (buf->print_handler != NULL) {
				buf->print_handler(buf, new_pos, buf->print_handler_ctx);
			}

			#if LOG_CBUFFER_APPEND_LOCK == true
				chMtxUnlock();
			#endif
			return LOG_CBUFFER_APPEND_MSG_OK;
		}


		/* last block is placed after the first block */
		if (new_pos > buf->blk_first) {
			//~ printf("append: last block after the first\n");
			/* check if there is enough space */
			if (length < (buf->size - new_pos)) {
				//~ printf("append: there is enough space, placing buffer here\n");
				/* if yes, place block here */
				place_ok = 1;
			} else {
				//~ printf("append: not enought space, wrapping\n");
				/* if not, wrap to the beginning */
				new_pos = 0;
				buf->d[buf->blk_last] |= LOG_HEADER_OVR;
			}
		} else {
			//~ printf("append: last block before the first\n");
			/* check if there is enough space */
			if (length < (buf->blk_first - new_pos)) {
				//~ printf("append: there is enough space, placing buffer here\n");
				/* if yes, place block here */
				place_ok = 1;
			} else {
				//~ printf("append: not enought space, truncating\n");
				/* if not, free some space */
				log_cbuffer_truncate(buf);
			}

		}
	}

	#if LOG_CBUFFER_APPEND_LOCK == true
		chMtxUnlock();
	#endif

}


int32_t log_cbuffer_printf(struct log_cbuffer *buf, uint8_t type, char *fmt, ...) {

	va_list args;
	char str[LOG_CBUFFER_MSG_LEN_MAX];

	va_start(args, fmt);
	vsnprintf(str, sizeof(str), fmt, args);
	va_end(args);

	log_cbuffer_append_msg(buf, str, type);

	return LOG_CBUFFER_PRINTF_OK;
}


int32_t log_cbuffer_truncate(struct log_cbuffer *buf) {
	uint32_t blk_next = buf->blk_first;
	log_cbuffer_next_block(buf, &blk_next);
	buf->blk_first = blk_next;

	return LOG_CBUFFER_TRUNCATE_OK;
}


int32_t log_cbuffer_set_print_handler(struct log_cbuffer *buf, void (*print_handler)(struct log_cbuffer *buf, uint32_t pos, void *ctx), void *ctx) {
	buf->print_handler = print_handler;
	buf->print_handler_ctx = ctx;

	return LOG_CBUFFER_SET_PRINT_HANDLER_OK;
}


int32_t log_cbuffer_set_time_handler(struct log_cbuffer *buf, void (*time_handler)(struct log_cbuffer *buf, uint32_t *time, void *ctx), void *ctx) {
	buf->time_handler = time_handler;
	buf->time_handler_ctx = ctx;

	return LOG_CBUFFER_SET_TIME_HANDLER_OK;
}


int32_t log_cbuffer_get_message(struct log_cbuffer *buf, uint32_t pos, char **msg) {
	*msg = (char *)(&(buf->d[pos + 8]));

	return LOG_CBUFFER_GET_MESSAGE_OK;
}


int32_t log_cbuffer_get_header(struct log_cbuffer *buf, uint32_t pos, uint8_t *header) {
	*header = buf->d[pos];

	return LOG_CBUFFER_GET_HEADER_OK;
}


int32_t log_cbuffer_get_time(struct log_cbuffer *buf, uint32_t pos, uint32_t *time) {
	uint32_t n = 0;
	n += buf->d[pos + 4] << 24;
	n += buf->d[pos + 5] << 16;
	n += buf->d[pos + 6] << 8;
	n += buf->d[pos + 7];

	*time = n;
	return LOG_CBUFFER_GET_TIME_OK;
}
