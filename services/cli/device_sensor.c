/* SPDX-License-Identifier: BSD-2-Clause
 *
 * /device/sensor configuration subtree
 *
 * Copyright (c) 2021, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#include <main.h>

/* Common functions and helpers for the CLI service. */
#include "cli_table_helper.h"
#include "cli.h"

/* Helper defines for tree construction. */
#include "services/cli/system_cli_tree.h"

#include <interfaces/servicelocator.h>
#include <interfaces/sensor.h>

#include "device_sensor.h"


static const struct cli_table_cell device_sensor_table[] = {
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* name */
	{.type = TYPE_STRING, .size = 32, .alignment = ALIGN_RIGHT}, /* description */
	{.type = TYPE_STRING, .size = 15, .alignment = ALIGN_RIGHT}, /* value */
	{.type = TYPE_STRING, .size = 10, .alignment = ALIGN_LEFT}, /* unit */
	{.type = TYPE_END}
};

static int32_t device_sensor_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, device_sensor_table, (const char *[]){
		"device name",
		"description",
		"value",
		"unit",
	});
	table_print_row_separator(cli->stream, device_sensor_table);

	Sensor *sensor;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_SENSOR, i, (Interface **)&sensor)) != ISERVICELOCATOR_RET_FAILED; i++) {
		const char *name = "";
		iservicelocator_get_name(locator, (Interface *)sensor, &name);

		char s[20] = {0};
		if (sensor->vmt->value_f) {
			float v = 0.0f;
			if (sensor->vmt->value_f(sensor, &v) == SENSOR_RET_OK) {
				snprintf(s, sizeof(s), "%.3f", v);
			}
		} else if (sensor->vmt->value_i32) {
			int32_t v = 0;
			if (sensor->vmt->value_i32(sensor, &v) == SENSOR_RET_OK) {
				snprintf(s, sizeof(s), "%ld", v);
			}
		}

		table_print_row(cli->stream, device_sensor_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = sensor->info->description},
			{.string = s},
			{.string = sensor->info->unit}
		});
	}

	return 0;
}





