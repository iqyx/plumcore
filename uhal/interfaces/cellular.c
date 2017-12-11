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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "u_assert.h"
#include "interface.h"

#include "cellular.h"

const char *cellular_modem_status_strings[] = {
	"not registered",
	"searching",
	"registered, home",
	"registered, roaming",
};


cellular_ret_t cellular_init(ICellular *self) {
	if (u_assert(self != NULL)) {
		return CELLULAR_RET_FAILED;
	}

	memset(self, 0, sizeof(ICellular));
	uhal_interface_init(&self->interface);

	return CELLULAR_RET_OK;
}


cellular_ret_t cellular_start(ICellular *self) {
	if (u_assert(self != NULL)) {
		return CELLULAR_RET_FAILED;
	}

	if (self->vmt.start != NULL) {
		return self->vmt.start(self->vmt.context);
	}
	return CELLULAR_RET_FAILED;
}


cellular_ret_t cellular_stop(ICellular *self) {
	if (u_assert(self != NULL)) {
		return CELLULAR_RET_FAILED;
	}

	if (self->vmt.stop != NULL) {
		return self->vmt.stop(self->vmt.context);
	}
	return CELLULAR_RET_FAILED;
}


cellular_ret_t cellular_imei(ICellular *self, char *imei) {
	if (u_assert(self != NULL)) {
		return CELLULAR_RET_FAILED;
	}

	if (self->vmt.imei != NULL) {
		return self->vmt.imei(self->vmt.context, imei);
	}
	return CELLULAR_RET_FAILED;
}


cellular_ret_t cellular_status(ICellular *self, enum cellular_modem_status *status) {
	if (u_assert(self != NULL) ||
	    u_assert(status != NULL)) {
		return CELLULAR_RET_FAILED;
	}

	if (self->vmt.status != NULL) {
		return self->vmt.status(self->vmt.context, status);
	}
	return CELLULAR_RET_FAILED;
}


cellular_ret_t cellular_get_operator(ICellular *self, char *operator) {
	if (u_assert(self != NULL) ||
	    u_assert(operator != NULL)) {
		return CELLULAR_RET_FAILED;
	}

	if (self->vmt.get_operator != NULL) {
		return self->vmt.get_operator(self->vmt.context, operator);
	}
	return CELLULAR_RET_FAILED;
}


cellular_ret_t cellular_run_ussd(ICellular *self, const char *request, char *response, size_t response_size) {
	if (u_assert(self != NULL) ||
	    u_assert(request != NULL) ||
	    u_assert(response != NULL)) {
		return CELLULAR_RET_FAILED;
	}

	if (self->vmt.run_ussd != NULL) {
		return self->vmt.run_ussd(self->vmt.context, request, response, response_size);
	}
	return CELLULAR_RET_FAILED;
}
