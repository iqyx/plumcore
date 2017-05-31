#include "hal_interface_directory.h"
#include "interface_power.h"
#include "cli_table_helper.h"
#include "treecli_parser.h"
#include "services/cli.h"


static const struct cli_table_cell device_power_table[] = {
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* name */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* device type */
	{.type = TYPE_INT32,  .size = 10, .alignment = ALIGN_RIGHT, .format = "%d mV"}, /* voltage */
	{.type = TYPE_INT32,  .size = 10, .alignment = ALIGN_RIGHT, .format = "%d mA"}, /* current */
	{.type = TYPE_INT32,  .size = 10, .alignment = ALIGN_RIGHT, .format = "%d mW"}, /* power */
	{.type = TYPE_UINT32, .size = 20, .alignment = ALIGN_RIGHT, .format = "%d %%"},  /* remaining capacity */
	{.type = TYPE_END}
};

static int32_t ucli_device_power_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, device_power_table, (const char *[]){
		"device name",
		"type",
		"voltage",
		"current",
		"power",
		"remaining capacity"
	});
	table_print_row_separator(cli->stream, device_power_table);

	for (size_t i = 0; hal_interfaces[i] != NULL; i++) {
		if (hal_interfaces[i]->type == HAL_INTERFACE_TYPE_POWER) {

			/* Extract interface pointer from the descriptor. */
			struct interface_power *iface = (struct interface_power *)(hal_interfaces[i]->iface_ptr);

			interface_power_measure(iface);
			table_print_row(cli->stream, device_power_table, (const union cli_table_cell_content []) {
				{.string = hal_interfaces[i]->name},
				{.string = "type"},
				{.int32 = interface_power_voltage(iface)},
				{.int32 = interface_power_current(iface)},
				{.int32 = interface_power_voltage(iface) * interface_power_current(iface) / 1000},
				{.uint32 = interface_power_capacity(iface)}
			});
		}
	}

	return 0;
}




