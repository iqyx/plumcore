/*
 * plumCore cellular modem/network interface
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
#include "interface.h"


typedef enum {
	CELLULAR_RET_OK = 0,
	CELLULAR_RET_FAILED = -1,
} cellular_ret_t;


enum cellular_modem_status {
	CELLULAR_MODEM_STATUS_NOT_REGISTERED = 0,
	CELLULAR_MODEM_STATUS_SEARCHING,
	CELLULAR_MODEM_STATUS_REGISTERED_HOME,
	CELLULAR_MODEM_STATUS_REGISTERED_ROAMING,

};

extern const char *cellular_modem_status_strings[];

struct interface_cellular_vmt {
	cellular_ret_t (*start)(void *context);
	cellular_ret_t (*stop)(void *context);
	cellular_ret_t (*imei)(void *context, char *imei);
	cellular_ret_t (*status)(void *context, enum cellular_modem_status *status);
	cellular_ret_t (*get_operator)(void *context, char *operator);

	void *context;
};


typedef struct {
	Interface interface;

	struct interface_cellular_vmt vmt;

} ICellular;


cellular_ret_t cellular_init(ICellular *self);
cellular_ret_t cellular_start(ICellular *self);
cellular_ret_t cellular_stop(ICellular *self);
cellular_ret_t cellular_imei(ICellular *self, char *imei);
cellular_ret_t cellular_status(ICellular *self, enum cellular_modem_status *status);
cellular_ret_t cellular_get_operator(ICellular *self, char *operator);


