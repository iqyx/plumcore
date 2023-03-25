/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Output adc-composite temperature coeff calibration data as a CSV
 *
 * Copyright (c) 2023, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#pragma once

#include <interfaces/applet.h>

#define SUBSCRIBE_TOPIC "channel/+"
#define FIRST_TOPIC "channel/temp"

extern const Applet tempco_calibration;
