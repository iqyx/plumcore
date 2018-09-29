#include "interfaces/sensor.h"
#include "cli_table_helper.h"
#include "cli.h"
#include "port.h"
#include "services/interfaces/servicelocator.h"


static const struct cli_table_cell device_sensor_table[] = {
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* name */
	{.type = TYPE_STRING, .size = 15, .alignment = ALIGN_RIGHT}, /* measured quantity */
	{.type = TYPE_STRING, .size = 15, .alignment = ALIGN_RIGHT}, /* measurement unit */
	{.type = TYPE_STRING, .size = 10, .alignment = ALIGN_RIGHT}, /* value */
	{.type = TYPE_STRING, .size = 5, .alignment = ALIGN_LEFT}, /* measurement unit symbol and order of magnitude */
	{.type = TYPE_END}
};

static int32_t device_sensor_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, device_sensor_table, (const char *[]){
		"device name",
		"quantity name",
		"unit name",
		"value",
		"unit",
	});
	table_print_row_separator(cli->stream, device_sensor_table);

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_SENSOR, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {
		ISensor *sensor = (ISensor *)interface;
		ISensorInfo info = {0};
		const char *name = "";
		float value;

		iservicelocator_get_name(locator, interface, &name);
		interface_sensor_info(sensor, &info);
		interface_sensor_value(sensor, &value);

		char value_str[20];
		snprintf(value_str, sizeof(value_str), "%ld.%03u", (int32_t)value, abs(value * 1000) % 1000);

		table_print_row(cli->stream, device_sensor_table, (const union cli_table_cell_content []) {
			{.string = name},
			{.string = info.quantity_name},
			{.string = info.unit_name},
			{.string = value_str},
			{.string = info.unit_symbol}
		});
	}

	return 0;
}





