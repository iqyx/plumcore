/*
 * File transfer protocol - default configuration
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

/**
 * Maximum length of a file name.
 */
#ifndef FT_MAX_FILE_NAME_SIZE
#define FT_MAX_FILE_NAME_SIZE           32
#endif

#ifndef FT_STEP_INTERVAL_MS
#define FT_STEP_INTERVAL_MS             50
#endif

#define FT_SESSION_INIT_TIMEOUT_MS      5000
#define FT_SESSION_RUNNING_TIMEOUT_MS   10000

#define FT_PROGRESS_UPDATE_INTERVAL_MS  500

#define FT_FILE_METADATA_MSG_INTERVAL   500
#define FT_FILE_REQUEST_MSG_INTERVAL    500


/**
 * How much time can pass from the last block request message before the piece is released from the cache.
 */
#define FT_SENDER_PIECE_IDLE_MAX        2000

/**
 * How much time can pass from the last block response received on the receiver side before a new block request
 * message is sent to the sender.
 */
#define FT_RECEIVER_PIECE_IDLE_MAX      500

/**
 * Nominal and maximum session ID size which can be handled. Session IDs for locally initiated transfers will be set
 * to the nominal ID size (FT_SESSION_ID_SIZE), but we are able to handle IDs with sizes up to FT_MAX_SESSION_ID_SIZE.
 */
#define FT_SESSION_ID_SIZE              2
#define FT_MAX_SESSION_ID_SIZE          8

/**
 * Number of pieces which can be processed simultaneously. At least 2 are recommended (a new piece transfer can start
 * while the old one is collecting lost blocks). Setting this number too high does not increase the transfer rate
 * significantly and consumes more memory.
 */
#define FT_PIECE_CACHE_COUNT            8

/**
 * Size of the bitmap field in a block request message. If the message has to fit inside the 64 byte message limit,
 * the bitmap size must be 32 bytes at most. Every byte in the bitmap corresponds to 8 block responses, each with
 * a single block of data.
 */
#define FT_MAX_PIECE_BITMAP_SIZE        4

/**
 * Maximum size of data in a block response message. Maximum piece size is calculated as maximum bitmap size
 * multiplied by maximum block size multiplied by 8. For 32 byte bitmap and 32 byte block size, maximum piece size
 * is 8192 bytes.
 */
#define FT_MAX_BLOCK_SIZE               32

#define FT_ENABLE_SUSPEND_RESUME        true

/**
 * File request messages may be disabled optionally. This can save some code space. If disabled, a node will not be
 * able to request a file from its peer, file sending will be available only.
 */
#define FT_ENABLE_FILE_REQUEST          true


