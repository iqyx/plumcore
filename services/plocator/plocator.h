/*
 * plumCore service locator
 *
 * Copyright (C) 2017, Marek Koza, qyx@krtko.org
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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "interface.h"
#include "interfaces/servicelocator.h"


typedef enum {
	PLOCATOR_RET_OK,
	PLOCATOR_RET_FAILED,
} plocator_ret_t;


struct plocator_item {
	Interface *interface;
	const char *name;
	enum iservicelocator_type type;

	struct plocator_item *next;
};


typedef struct {
	struct plocator_item *first;

	IServiceLocator iface;
} PLocator;


plocator_ret_t plocator_init(PLocator *self);
plocator_ret_t plocator_free(PLocator *self);
IServiceLocator *plocator_iface(PLocator *self);


