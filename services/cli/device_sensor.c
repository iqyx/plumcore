#include "uhal/interfaces/sensor.h"
#include "cli_table_helper.h"
#include "interface_directory.h"
#include "services/cli.h"
#include "port.h"


static const struct cli_table_cell device_sensor_table[] = {
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* name */
	{.type = TYPE_STRING, .size = 15, .alignment = ALIGN_RIGHT}, /* measured quantity */
	{.type = TYPE_STRING, .size = 15, .alignment = ALIGN_RIGHT}, /* measurement unit */
	{.type = TYPE_INT32,  .size = 10, .alignment = ALIGN_RIGHT, .format = "%d"}, /* value */
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

	enum interface_directory_type t;
	for (size_t i = 0; (t = interface_directory_get_type(&interfaces, i)) != INTERFACE_TYPE_NONE; i++) {


		if (t == INTERFACE_TYPE_SENSOR) {
			ISensor *sensor = (ISensor *)interface_directory_get_interface(&interfaces, i);
			ISensorInfo info;
			const char *name = interface_directory_get_name(&interfaces, i);
			float value;

			interface_sensor_info(sensor, &info);
			interface_sensor_value(sensor, &value);

			table_print_row(cli->stream, device_sensor_table, (const union cli_table_cell_content []) {
				{.string = name},
				{.string = info.quantity_name},
				{.string = info.unit_name},
				{.int32 = (int32_t)value},
				{.string = info.unit_symbol}
			});
		}
	}

	return 0;
}





