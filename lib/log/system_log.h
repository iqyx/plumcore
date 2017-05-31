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

#ifndef _SYSTEM_LOG_H_
#define _SYSTEM_LOG_H_

#include <stdint.h>
#include <stdbool.h>


#define LOG_CBUFFER_APPEND_LOCK       false

#define LOG_TYPE_NULL 0
#define LOG_TYPE_INFO 1
#define LOG_TYPE_DEBUG 2
#define LOG_TYPE_WARN 3
#define LOG_TYPE_ERROR 4
#define LOG_TYPE_CRIT 5
#define LOG_TYPE_ASSERT 6

#define LOG_HEADER_OVR 0x80

#define LOG_CBUFFER_MSG_LEN_MAX 128


struct log_cbuffer {
	uint32_t blk_first;
	uint32_t blk_last;
	uint32_t size;
	unsigned char *d;
	void (*print_handler)(struct log_cbuffer *buf, uint32_t pos, void *ctx);
	void *print_handler_ctx;
	void (*time_handler)(struct log_cbuffer *buf, uint32_t *time, void *ctx);
	void *time_handler_ctx;

	#if LOG_CBUFFER_APPEND_LOCK == true
		Mutex add_mutex;
	#endif
};


int32_t log_cbuffer_init(struct log_cbuffer *buf, uint8_t *data, uint32_t size);
#define LOG_CBUFFER_INIT_OK 0

int32_t log_cbuffer_set_header(struct log_cbuffer *buf, uint32_t pos, uint8_t header);
#define LOG_CBUFFER_SET_HEADER_OK 0

int32_t log_cbuffer_set_time(struct log_cbuffer *buf, uint32_t pos, uint32_t time);
#define LOG_CBUFFER_SET_TIME_OK 0

int32_t log_cbuffer_set_message(struct log_cbuffer *buf, uint32_t pos, char *message, uint32_t len);
#define LOG_CBUFFER_SET_MSG_OK 0
#define LOG_CBUFFER_SET_MSG_TOO_LONG -1

int32_t log_cbuffer_print(struct log_cbuffer *buf);
#define LOG_CBUFFER_PRINT_OK 0

int32_t log_cbuffer_next_block(struct log_cbuffer *buf, uint32_t *pos);
#define LOG_CBUFFER_NEXT_BLK_OK 0

int32_t log_cbuffer_append_msg(struct log_cbuffer *buf, char *msg, uint8_t type);
#define LOG_CBUFFER_APPEND_MSG_OK 0

int32_t log_cbuffer_truncate(struct log_cbuffer *buf);
#define LOG_CBUFFER_TRUNCATE_OK 0

int32_t log_cbuffer_set_print_handler(struct log_cbuffer *buf, void (*print_handler)(struct log_cbuffer *buf, uint32_t pos, void *ctx), void *ctx);
#define LOG_CBUFFER_SET_PRINT_HANDLER_OK 0

int32_t log_cbuffer_set_time_handler(struct log_cbuffer *buf, void (*time_handler)(struct log_cbuffer *buf, uint32_t *time, void *ctx), void *ctx);
#define LOG_CBUFFER_SET_TIME_HANDLER_OK 0

int32_t log_cbuffer_get_message(struct log_cbuffer *buf, uint32_t pos, char **msg);
#define LOG_CBUFFER_GET_MESSAGE_OK 0

int32_t log_cbuffer_get_header(struct log_cbuffer *buf, uint32_t pos, uint8_t *header);
#define LOG_CBUFFER_GET_HEADER_OK 0

int32_t log_cbuffer_get_time(struct log_cbuffer *buf, uint32_t pos, uint32_t *time);
#define LOG_CBUFFER_GET_TIME_OK 0

int32_t log_cbuffer_printf(struct log_cbuffer *buf, uint8_t type, char *fmt, ...);
#define LOG_CBUFFER_PRINTF_OK 0


#endif
