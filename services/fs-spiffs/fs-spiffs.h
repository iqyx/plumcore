/*
 * SPIFFS filesystem library wrapper service
 *
 * Copyright (c) 2018, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "spiffs.h"
#include <interfaces/flash.h>
#include <interfaces/fs.h>


#define LOG_PAGE_SIZE 256

typedef enum {
	FS_SPIFFS_RET_OK = 0,
	FS_SPIFFS_RET_FAILED,
	FS_SPIFFS_RET_NULL,
	FS_SPIFFS_RET_BAD_ARG,
	FS_SPIFFS_RET_BAD_STATE,
} fs_spiffs_ret_t;


typedef enum {
	FS_SPIFFS_STATE_UNINITIALIZED = 0,
	FS_SPIFFS_STATE_INITIALIZED,
	FS_SPIFFS_STATE_MOUNTED,
} fs_spiffs_state_t;


typedef struct fs_spiffs {
	/* This must be the first member in the struct. */
	spiffs spiffs;
	spiffs_config cfg;

	/* SPIFFS work area. */
	u8_t spiffs_work_buf[LOG_PAGE_SIZE * 2];
	u8_t spiffs_fds[32 * 4];
	u8_t spiffs_cache_buf[(LOG_PAGE_SIZE + 32) * 4];

	Flash *flash;
	IFs iface;

	fs_spiffs_state_t state;
} FsSpiffs;



fs_spiffs_ret_t fs_spiffs_init(FsSpiffs *self);
fs_spiffs_ret_t fs_spiffs_free(FsSpiffs *self);
fs_spiffs_ret_t fs_spiffs_mount(FsSpiffs *self, Flash *flash);
fs_spiffs_ret_t fs_spiffs_unmount(FsSpiffs *self);
fs_spiffs_ret_t fs_spiffs_format(FsSpiffs *self, Flash *flash);
fs_spiffs_ret_t fs_spiffs_check(FsSpiffs *self);
IFs *fs_spiffs_interface(FsSpiffs *self);

