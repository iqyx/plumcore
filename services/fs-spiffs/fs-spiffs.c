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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "main.h"

#include "fs-spiffs.h"
#include "spiffs.h"
#include "interfaces/flash.h"
#include "interfaces/fs.h"


#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "fs-spiffs"


/**
 * SPIFFS HAL functions to read, write and erase the flash memory.
 * They are using the IFlash interface to access it.
 */

static s32_t fs_spiffs_read(struct spiffs_t *context, u32_t addr, u32_t size, u8_t *dst) {
	FsSpiffs *self = (FsSpiffs *)context;
	self->flash->vmt->read(self->flash, addr, (void *)dst, size);
	/** @todo check iflash_read return value */
	return SPIFFS_OK;
}


static s32_t fs_spiffs_write(struct spiffs_t *context, u32_t addr, u32_t size, u8_t *src) {
	FsSpiffs *self = (FsSpiffs *)context;
	self->flash->vmt->write(self->flash, addr, src, size);
	return SPIFFS_OK;
}


static s32_t fs_spiffs_erase(struct spiffs_t *context, u32_t addr, u32_t size) {
	/** @todo check if size == sector_size */
	(void)size;
	FsSpiffs *self = (FsSpiffs *)context;
	self->flash->vmt->erase(self->flash, addr, size);
	return SPIFFS_OK;
}


static void fs_spiffs_check_callback(struct spiffs_t *context, spiffs_check_type type, spiffs_check_report report, u32_t arg1, u32_t arg2) {
	if (report != SPIFFS_CHECK_PROGRESS) {
		u_log(system_log, LOG_TYPE_DEBUG, U_LOG_MODULE_PREFIX("check type=%u report=%u arg1=%u arg2=%u"), type, report, arg1, arg2);
	}
}

/**
 * IFs filesystem interface implementation
 */

static ifs_ret_t fs_spiffs_ifs_open(void *context, IFsFile *f, const char *filename, uint32_t mode) {
	FsSpiffs *self = (FsSpiffs *)context;
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return IFS_RET_FAILED;
	}

	uint32_t smode = 0;
	if (mode & IFS_MODE_APPEND) smode |= SPIFFS_APPEND;
	if (mode & IFS_MODE_TRUNCATE) smode |= SPIFFS_TRUNC;
	if (mode & IFS_MODE_CREATE) smode |= SPIFFS_CREAT;
	if (mode & IFS_MODE_WRITEONLY) smode |= SPIFFS_WRONLY;
	if (mode & IFS_MODE_READONLY) smode |= SPIFFS_RDONLY;
	if (mode & IFS_MODE_READWRITE) smode |= SPIFFS_RDWR;

	spiffs_file sf = SPIFFS_open(&self->spiffs, filename, smode, 0);
	if (sf < 0) {
		/** @todo proper return code */
		return IFS_RET_FAILED;
	}

	*f = (IFsFile)sf;
	return IFS_RET_OK;
}


static ifs_ret_t fs_spiffs_ifs_close(void *context, IFsFile f) {
	FsSpiffs *self = (FsSpiffs *)context;
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return IFS_RET_FAILED;
	}

	SPIFFS_clearerr(&self->spiffs);
	SPIFFS_close(&self->spiffs, (spiffs_file)f);
	if (SPIFFS_errno(&self->spiffs) != 0) {
		return IFS_RET_FAILED;
	}
	return IFS_RET_OK;
}


static ifs_ret_t fs_spiffs_ifs_remove(void *context, const char *filename) {
	FsSpiffs *self = (FsSpiffs *)context;
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return IFS_RET_FAILED;
	}

	SPIFFS_clearerr(&self->spiffs);
	SPIFFS_remove(&self->spiffs, filename);
	if (SPIFFS_errno(&self->spiffs) != 0) {
		return IFS_RET_FAILED;
	}
	return IFS_RET_OK;
}


static ifs_ret_t fs_spiffs_ifs_rename(void *context, const char *old_fn, const char *new_fn) {
	FsSpiffs *self = (FsSpiffs *)context;
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return IFS_RET_FAILED;
	}

	SPIFFS_clearerr(&self->spiffs);
	SPIFFS_rename(&self->spiffs, old_fn, new_fn);
	if (SPIFFS_errno(&self->spiffs) != 0) {
		return IFS_RET_FAILED;
	}
	return IFS_RET_OK;
}


static ifs_ret_t fs_spiffs_ifs_read(void *context, IFsFile f, uint8_t *buf, size_t len, size_t *read) {
	FsSpiffs *self = (FsSpiffs *)context;
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return IFS_RET_FAILED;
	}

	SPIFFS_clearerr(&self->spiffs);
	if (read != NULL) {
		*read = SPIFFS_read(&self->spiffs, (spiffs_file)f, (void *)buf, len);
	} else {
		SPIFFS_read(&self->spiffs, (spiffs_file)f, (void *)buf, len);
	}
	if (SPIFFS_errno(&self->spiffs) != 0) {
		return IFS_RET_FAILED;
	}
	return IFS_RET_OK;
}


static ifs_ret_t fs_spiffs_ifs_write(void *context, IFsFile f, const uint8_t *buf, size_t len, size_t *written) {
	FsSpiffs *self = (FsSpiffs *)context;
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return IFS_RET_FAILED;
	}

	SPIFFS_clearerr(&self->spiffs);
	if (written != NULL) {
		*written = SPIFFS_write(&self->spiffs, (spiffs_file)f, (void *)buf, len);
	} else {
		SPIFFS_write(&self->spiffs, (spiffs_file)f, (void *)buf, len);
	}
	if (SPIFFS_errno(&self->spiffs) != 0) {
		return IFS_RET_FAILED;
	}
	return IFS_RET_OK;
}


static ifs_ret_t fs_spiffs_ifs_flush(void *context, IFsFile f) {
	FsSpiffs *self = (FsSpiffs *)context;
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return IFS_RET_FAILED;
	}

	SPIFFS_fflush(&self->spiffs, (spiffs_file)f);
	if (SPIFFS_errno(&self->spiffs) != 0) {
		return IFS_RET_FAILED;
	}
	return IFS_RET_OK;
}


fs_spiffs_ret_t fs_spiffs_init(FsSpiffs *self) {
	if (u_assert(self != NULL)) {
		return FS_SPIFFS_RET_NULL;
	}

	memset(self, 0, sizeof(FsSpiffs));
	ifs_init(&self->iface);

	self->iface.vmt.context = (void *)self;
	self->iface.vmt.open = fs_spiffs_ifs_open;
	self->iface.vmt.close = fs_spiffs_ifs_close;
	self->iface.vmt.remove = fs_spiffs_ifs_remove;
	self->iface.vmt.rename = fs_spiffs_ifs_rename;
	self->iface.vmt.read = fs_spiffs_ifs_read;
	self->iface.vmt.write = fs_spiffs_ifs_write;
	self->iface.vmt.flush = fs_spiffs_ifs_flush;

	self->state = FS_SPIFFS_STATE_INITIALIZED;
	return FS_SPIFFS_RET_OK;
}


fs_spiffs_ret_t fs_spiffs_free(FsSpiffs *self) {
	if (u_assert(self != NULL)) {
		return FS_SPIFFS_RET_NULL;
	}
	/* Unmount is required before free. */
	if (u_assert(self->state == FS_SPIFFS_STATE_INITIALIZED)) {
		return FS_SPIFFS_RET_BAD_STATE;
	}

	ifs_free(&self->iface);

	return FS_SPIFFS_RET_OK;
}


fs_spiffs_ret_t fs_spiffs_mount(FsSpiffs *self, Flash *flash) {
	if (u_assert(self != NULL)) {
		return FS_SPIFFS_RET_NULL;
	}
	if (u_assert(flash != NULL)) {
		return FS_SPIFFS_RET_BAD_ARG;
	}
	if (u_assert(self->state == FS_SPIFFS_STATE_INITIALIZED)) {
		return FS_SPIFFS_RET_BAD_STATE;
	}

	self->flash = flash;

	size_t flash_size = 0;
	flash_block_ops_t ops = 0;
	self->flash->vmt->get_size(self->flash, 0, &flash_size, &ops);

	/** @todo get suitable erase block size */
	size_t erase_size = 0;
	self->flash->vmt->get_size(self->flash, 2, &erase_size, &ops);

	/* SPIFFS configuration structure. */
	self->cfg.phys_size = flash_size;
	self->cfg.phys_addr = 0;
	self->cfg.phys_erase_block = erase_size;
	self->cfg.log_block_size = erase_size;
	self->cfg.log_page_size = LOG_PAGE_SIZE;

	self->cfg.hal_read_f = fs_spiffs_read;
	self->cfg.hal_write_f = fs_spiffs_write;
	self->cfg.hal_erase_f = fs_spiffs_erase;

	int32_t ret = SPIFFS_mount(&self->spiffs,
		&self->cfg,
		self->spiffs_work_buf,
		self->spiffs_fds,
		sizeof(self->spiffs_fds),
		self->spiffs_cache_buf,
		sizeof(self->spiffs_cache_buf),
		fs_spiffs_check_callback
	);

	if (ret != 0) {
		u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("SPIFFS failed to mount, error = %d"), ret);
		return FS_SPIFFS_RET_FAILED;
	}


	uint32_t total = 0;
	uint32_t used = 0;
	SPIFFS_info(&self->spiffs, &total, &used);
	u_log(system_log, LOG_TYPE_INFO,
		U_LOG_MODULE_PREFIX("SPIFFS filesystem mounted, total = %luK, used = %luK"),
		total / 1024,
		used / 1024
	);

	self->state = FS_SPIFFS_STATE_MOUNTED;

	fs_spiffs_check(self);

	return FS_SPIFFS_RET_OK;
}


fs_spiffs_ret_t fs_spiffs_unmount(FsSpiffs *self) {
	if (u_assert(self != NULL)) {
		return FS_SPIFFS_RET_NULL;
	}
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return FS_SPIFFS_RET_BAD_STATE;
	}

	SPIFFS_unmount(&self->spiffs);

	self->state = FS_SPIFFS_STATE_INITIALIZED;
	return FS_SPIFFS_RET_OK;
}


fs_spiffs_ret_t fs_spiffs_format(FsSpiffs *self, Flash *flash) {
	if (u_assert(self != NULL)) {
		return FS_SPIFFS_RET_NULL;
	}
	if (u_assert(flash != NULL)) {
		return FS_SPIFFS_RET_BAD_ARG;
	}
	if (u_assert(self->state == FS_SPIFFS_STATE_INITIALIZED)) {
		return FS_SPIFFS_RET_BAD_STATE;
	}

	/* Try to mount first to configure SPIFFS. */
	if (fs_spiffs_mount(self, flash) == FS_SPIFFS_RET_OK) {
		SPIFFS_unmount(&self->spiffs);
		self->state = FS_SPIFFS_STATE_INITIALIZED;
	}
	if (SPIFFS_format(&self->spiffs) != 0) {
		return FS_SPIFFS_RET_FAILED;
	}

	/* Keep the state as is. */
	return FS_SPIFFS_RET_OK;
}


fs_spiffs_ret_t fs_spiffs_check(FsSpiffs *self) {
	if (u_assert(self != NULL)) {
		return FS_SPIFFS_RET_NULL;
	}
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return FS_SPIFFS_RET_BAD_STATE;
	}

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("running SPIFFS check"));
	SPIFFS_check(&self->spiffs);

	return FS_SPIFFS_RET_OK;
}


IFs *fs_spiffs_interface(FsSpiffs *self) {
	if (u_assert(self != NULL)) {
		return NULL;
	}
	if (u_assert(self->state == FS_SPIFFS_STATE_MOUNTED)) {
		return NULL;
	}

	return &self->iface;
}
