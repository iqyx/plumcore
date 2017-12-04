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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "u_assert.h"
#include "u_log.h"

#include "interface.h"
#include "plocator.h"

#ifdef MODULE_NAME
#undef MODULE_NAME
#endif
#define MODULE_NAME "plocator"



static iservicelocator_ret_t plocator_add(
	void *context,
	enum iservicelocator_type type,
	Interface * interface,
	char * name
) {
	PLocator *self = (PLocator *)context;

	struct plocator_item *item = malloc(sizeof(struct plocator_item));
	if (item == NULL) {
		return ISERVICELOCATOR_RET_FAILED;
	}
	item->next = self->first;
	item->interface = interface;
	item->name = name;
	item->type = type;

	/* Insert the newly created item at the beginning. */
	self->first = item;

	return ISERVICELOCATOR_RET_OK;
}


static iservicelocator_ret_t plocator_query_name(
	void *context,
	char * name,
	Interface ** result
) {
	PLocator *self = (PLocator *)context;

	struct plocator_item *item = self->first;
	while (item != NULL) {
		if (!strcmp(name, item->name)) {
			*result = item->interface;
			return ISERVICELOCATOR_RET_OK;
		}
		item = item->next;
	}

	return ISERVICELOCATOR_RET_FAILED;
}


static iservicelocator_ret_t plocator_query_type_id(
	void *context,
	enum iservicelocator_type type,
	size_t index,
	Interface ** result
) {
	PLocator *self = (PLocator *)context;

	struct plocator_item *item = self->first;
	size_t current_id = 0;
	while (item != NULL) {
		if (item->type == type) {
			if (current_id == index) {
				*result = item->interface;
				return ISERVICELOCATOR_RET_OK;
			}
			current_id++;
		}
		item = item->next;
	}

	return ISERVICELOCATOR_RET_FAILED;
}


static iservicelocator_ret_t plocator_query_type_next(
	void *context,
	enum iservicelocator_type type,
	Interface * start,
	Interface ** result
) {
	(void)context;
	(void)type;
	(void)start;
	(void)result;

	return ISERVICELOCATOR_RET_NOT_IMPLEMENTED;
}


static iservicelocator_ret_t plocator_get_name(
	void *context,
	Interface * interface,
	const char ** result
) {
	PLocator *self = (PLocator *)context;

	struct plocator_item *item = self->first;
	while (item != NULL) {
		if (item->interface == interface) {
			*result = item->name;
			return ISERVICELOCATOR_RET_OK;
		}
		item = item->next;
	}

	return ISERVICELOCATOR_RET_FAILED;
}


plocator_ret_t plocator_init(PLocator *self) {
	if (u_assert(self != NULL)) {
		return PLOCATOR_RET_FAILED;
	}

	memset(self, 0, sizeof(PLocator));
	if (iservicelocator_init(&self->iface) != ISERVICELOCATOR_RET_OK) {
		goto err;
	}
	self->iface.vmt.context = (void *)self;
	self->iface.vmt.add = plocator_add;
	self->iface.vmt.query_name = plocator_query_name;
	self->iface.vmt.query_type_id = plocator_query_type_id;
	self->iface.vmt.query_type_next = plocator_query_type_next;
	self->iface.vmt.get_name = plocator_get_name;

	u_log(system_log, LOG_TYPE_INFO, U_LOG_MODULE_PREFIX("service initialized"));
	return PLOCATOR_RET_OK;
err:

	u_log(system_log, LOG_TYPE_ERROR, U_LOG_MODULE_PREFIX("service initialization failed"));
	return PLOCATOR_RET_FAILED;
}


plocator_ret_t plocator_free(PLocator *self) {
	if (u_assert(self != NULL)) {
		return PLOCATOR_RET_FAILED;
	}

	iservicelocator_free(&self->iface);

	return PLOCATOR_RET_OK;
}


IServiceLocator *plocator_iface(PLocator *self) {
	if (u_assert(self != NULL)) {
		return NULL;
	}

	return &self->iface;
}

