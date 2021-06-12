/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Generic filesystem interface
 *
 * Copyright (c) 2018-2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <stdint.h>

typedef enum fs_mode {
	FS_MODE_NONE = 0,
	FS_MODE_APPEND = (1 << 0),
	FS_MODE_TRUNCATE = (1 << 1),
	FS_MODE_CREATE = (1 << 2),
	FS_MODE_READONLY = (1 << 3),
	FS_MODE_WRITEONLY = (1 << 4),
	FS_MODE_READWRITE = (1 << 5),
} fs_mode_t;

typedef enum fs_ret {
	FS_RET_OK = 0,
	FS_RET_FAILED,
	FS_RET_NULL,
} fs_ret_t;

typedef enum fs_seek {
	FS_SEEK_SET,
	FS_SEEK_CUR,
	FS_SEEK_END,
} fs_seek_t;

typedef struct fs_file {
	uint32_t handle;
	void *ptr;
} File;

typedef struct fs_dir {
	void *ptr;
} Dir;

typedef struct fs_stat {
	uint32_t handle;
	char *name;
	size_t name_size;
	size_t size;
} fs_stat_t;

typedef struct fs_info {
	size_t size_total;
	size_t size_used;
} fs_info_t;

typedef struct fs_dir_entry {
	uint32_t handle;
	void *ptr;
	char *name;
	size_t name_size;
	size_t size;
} DirEntry;


typedef struct fs Fs;
struct fs_vmt {
	fs_ret_t (*open)(Fs *self, File *f, const char *path, enum fs_mode mode);
	fs_ret_t (*read)(Fs *self, File *f, void *buf, size_t len, size_t *read);
	fs_ret_t (*write)(Fs *self, File *f, const void *buf, size_t len, size_t *written);
	fs_ret_t (*lseek)(Fs *self, File *f, size_t offset, enum fs_seek whence);
	fs_ret_t (*remove)(Fs *self, const char *path);
	fs_ret_t (*fremove)(Fs *self, File *f);
	fs_ret_t (*stat)(Fs *self, const char *path, struct fs_stat *s);
	fs_ret_t (*fstat)(Fs *self, File *f, struct fs_stat *s);
	fs_ret_t (*fflush)(Fs *self, File *f);
	fs_ret_t (*close)(Fs *self, File *f);
	fs_ret_t (*rename)(Fs *self, const char *old_path, const char *new_path);
	fs_ret_t (*info)(Fs *self, struct fs_info *info);
	fs_ret_t (*opendir)(Fs *self, Dir *d, const char *path);
	fs_ret_t (*closedir)(Fs *self, Dir *d);
	fs_ret_t (*readdir)(Fs *self, Dir *d, DirEntry *e);
};

typedef struct fs {
	const struct fs_vmt *vmt;
	void *parent;
} Fs;


