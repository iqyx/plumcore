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

#pragma once

#include <stdlib.h>
#include "u_assert.h"
#include "interface.h"


typedef enum {
	INTERFACE_DIRECTORY_RET_OK = 0,
	INTERFACE_DIRECTORY_RET_FAILED = -1,
} interface_directory_ret_t;

enum interface_directory_type {
	INTERFACE_TYPE_NONE = 0,
	INTERFACE_TYPE_SENSOR,
};

struct interface_directory_item {
	Interface *interface;
	const char *name;
	enum interface_directory_type type;
};

typedef struct {
	struct interface_directory_item *interfaces;

} InterfaceDirectory;

extern InterfaceDirectory interface_directory;


interface_directory_ret_t interface_directory_init(InterfaceDirectory *self, struct interface_directory_item *interfaces);
interface_directory_ret_t interface_directory_free(InterfaceDirectory *self);
Interface *interface_directory_get_interface(InterfaceDirectory *self, size_t id);
enum interface_directory_type interface_directory_get_type(InterfaceDirectory *self, size_t id);
const char *interface_directory_get_name(InterfaceDirectory *self, size_t id);
