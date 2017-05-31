/*
 * Copyright (c) 2017, Marek Koza (qyx@krtko.org)
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

#include <stdlib.h>
#include "interface_directory.h"

interface_directory_ret_t interface_directory_init(InterfaceDirectory *self, struct interface_directory_item *interfaces) {
	if (u_assert(self != NULL) ||
	    u_assert(interfaces != NULL)) {
		return INTERFACE_DIRECTORY_RET_FAILED;
	}

	self->interfaces = interfaces;

	return INTERFACE_DIRECTORY_RET_OK;
}


interface_directory_ret_t interface_directory_free(InterfaceDirectory *self) {
	if (u_assert(self != NULL)) {
		return INTERFACE_DIRECTORY_RET_FAILED;

	}

	/* Do nothing. */

	return INTERFACE_DIRECTORY_RET_OK;
}


Interface *interface_directory_get_interface(InterfaceDirectory *self, size_t id) {
	if (u_assert(self != NULL)) {
		return NULL;
	}

	if (self->interfaces[id].type == INTERFACE_TYPE_NONE) {
		return NULL;
	} else {
		return self->interfaces[id].interface;
	}
}


enum interface_directory_type interface_directory_get_type(InterfaceDirectory *self, size_t id) {
	if (u_assert(self != NULL)) {
		return INTERFACE_TYPE_NONE;
	}

	return self->interfaces[id].type;
}


const char *interface_directory_get_name(InterfaceDirectory *self, size_t id) {
	if (u_assert(self != NULL)) {
		return "?";
	}

	if (self->interfaces[id].name == NULL) {
		return "?";
	} else {
		return self->interfaces[id].name;
	}
}
