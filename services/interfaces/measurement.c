/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Measurement data interface
 *
 * Copyright (c) 2026, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#include "measurement.h"


measurement_ret_t measurement_init(Measurement *self) {
	memset(self, 0, sizeof(Measurement));
	return MEASUREMENT_RET_OK;
}


measurement_ret_t measurement_free(Measurement *self) {
	(void)self;
	return MEASUREMENT_RET_OK;
}
