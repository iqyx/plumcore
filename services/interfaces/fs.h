/*
 * Generic filesystem interface
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
#include "interface.h"


#define IFS_MODE_APPEND 1
#define IFS_MODE_TRUNCATE 2
#define IFS_MODE_CREATE 4
#define IFS_MODE_READONLY 8
#define IFS_MODE_WRITEONLY 16
#define IFS_MODE_READWRITE 32

typedef enum {
	IFS_RET_OK = 0,
	IFS_RET_FAILED,
	IFS_RET_NULL,
} ifs_ret_t;

typedef uint32_t IFsFile;
struct ifs_vmt {
	ifs_ret_t (*open)(void *context, IFsFile *f, const char *filename, uint32_t mode);
	ifs_ret_t (*close)(void *context, IFsFile f);
	ifs_ret_t (*remove)(void *context, const char *filename);
	ifs_ret_t (*rename)(void *context, const char *old_fn, const char *new_fn);
	ifs_ret_t (*read)(void *context, IFsFile f, uint8_t *buf, size_t len, size_t *read);
	ifs_ret_t (*write)(void *context, IFsFile f, const uint8_t *buf, size_t len, size_t *written);
	ifs_ret_t (*flush)(void *context, IFsFile f);
	ifs_ret_t (*stats)(void *context, size_t *total, size_t *used);

	void *context;
};

typedef struct ifs {
	Interface interface;

	struct ifs_vmt vmt;
} IFs;


ifs_ret_t ifs_init(IFs *self);
ifs_ret_t ifs_free(IFs *self);

ifs_ret_t ifs_fread(IFs *self, IFsFile f, uint8_t *buf, size_t len, size_t *read);
ifs_ret_t ifs_fwrite(IFs *self, IFsFile f, const uint8_t *buf, size_t len, size_t *written);
ifs_ret_t ifs_fflush(IFs *self, IFsFile f);
ifs_ret_t ifs_fclose(IFs *self, IFsFile f);

ifs_ret_t ifs_open(IFs *self, IFsFile *f, const char *filename, uint32_t mode);
ifs_ret_t ifs_remove(IFs *self, const char *filename);
ifs_ret_t ifs_rename(IFs *self, const char *old_fn, const char *new_fn);
ifs_ret_t ifs_stats(IFs *self, size_t *total, size_t *used);
