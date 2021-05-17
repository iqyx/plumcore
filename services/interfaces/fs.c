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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "u_assert.h"
#include "interface.h"

#include "fs.h"


ifs_ret_t ifs_init(IFs *self) {
	if (u_assert(self != NULL)) {
		return IFS_RET_FAILED;
	}

	memset(self, 0, sizeof(IFs));
	uhal_interface_init(&self->interface);

	return IFS_RET_OK;
}


ifs_ret_t ifs_free(IFs *self) {
	if (u_assert(self != NULL)) {
		return IFS_RET_FAILED;
	}

	/* Nothing more to free now. */

	return IFS_RET_OK;
}


ifs_ret_t ifs_fread(IFs *self, IFsFile f, uint8_t *buf, size_t len, size_t *read) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len > 0)) {
		return IFS_RET_FAILED;
	}

	if (self->vmt.read != NULL) {
		return self->vmt.read(self->vmt.context, f, buf, len, read);
	}

	return IFS_RET_FAILED;
}


ifs_ret_t ifs_fwrite(IFs *self, IFsFile f, const uint8_t *buf, size_t len, size_t *written) {
	if (u_assert(self != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len > 0)) {
		return IFS_RET_FAILED;
	}

	if (self->vmt.write != NULL) {
		return self->vmt.write(self->vmt.context, f, buf, len, written);
	}

	return IFS_RET_FAILED;
}


ifs_ret_t ifs_fflush(IFs *self, IFsFile f) {
	if (u_assert(self != NULL)) {
		return IFS_RET_FAILED;
	}

	if (self->vmt.flush != NULL) {
		return self->vmt.flush(self->vmt.context, f);
	}

	return IFS_RET_FAILED;
}


ifs_ret_t ifs_fclose(IFs *self, IFsFile f) {
	if (u_assert(self != NULL)) {
		return IFS_RET_FAILED;
	}

	if (self->vmt.close != NULL) {
		return self->vmt.close(self->vmt.context, f);
	}

	return IFS_RET_FAILED;
}


ifs_ret_t ifs_open(IFs *self, IFsFile *f, const char *filename, uint32_t mode) {
	if (u_assert(self != NULL) ||
	    u_assert(filename != NULL)) {
		return IFS_RET_FAILED;
	}

	if (self->vmt.open != NULL) {
		return self->vmt.open(self->vmt.context, f, filename, mode);
	}

	return IFS_RET_FAILED;
}


ifs_ret_t ifs_remove(IFs *self, const char *filename) {
	if (u_assert(self != NULL) ||
	    u_assert(filename != NULL)) {
		return IFS_RET_FAILED;
	}

	if (self->vmt.remove != NULL) {
		return self->vmt.remove(self->vmt.context, filename);
	}

	return IFS_RET_FAILED;
}


ifs_ret_t ifs_rename(IFs *self, const char *old_fn, const char *new_fn) {
	if (u_assert(self != NULL) ||
	    u_assert(old_fn != NULL) ||
	    u_assert(new_fn != NULL)) {
		return IFS_RET_FAILED;
	}

	if (self->vmt.rename != NULL) {
		return self->vmt.rename(self->vmt.context, old_fn, new_fn);
	}

	return IFS_RET_FAILED;
}

ifs_ret_t ifs_stats(IFs *self, size_t *total, size_t *used) {
	if (u_assert(self != NULL) ||
	    u_assert(total != NULL) ||
	    u_assert(used != NULL)) {
		return IFS_RET_FAILED;
	}

	if (self->vmt.stats != NULL) {
		return self->vmt.stats(self->vmt.context, total, used);
	}

	return IFS_RET_FAILED;
}
