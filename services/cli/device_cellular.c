#include "uhal/interfaces/cellular.h"
#include "cli_table_helper.h"
#include "interface_directory.h"
#include "services/cli.h"
#include "port.h"


int32_t device_cellular_cellularN_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf) {
	u_log(system_log, LOG_TYPE_INFO, "set enabled");

}


static const struct treecli_value *device_cellular_cellularN_values[] = {
	&(const struct treecli_value) {
		.name = "enabled",
		.set = device_cellular_cellularN_enabled_set,
	},
	NULL
};

static const struct cli_table_cell device_cellular_table[] = {
	{.type = TYPE_STRING, .size = 12, .alignment = ALIGN_RIGHT}, /* name */
	{.type = TYPE_STRING, .size = 16, .alignment = ALIGN_RIGHT}, /* imei */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* status */
	{.type = TYPE_STRING, .size = 20, .alignment = ALIGN_RIGHT}, /* operator */

	{.type = TYPE_END}
};

static int32_t device_cellular_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	table_print_header(cli->stream, device_cellular_table, (const char *[]){
		"Device name",
		"IMEI",
		"Status",
		"Operator",
	});
	table_print_row_separator(cli->stream, device_cellular_table);

	enum interface_directory_type t;
	for (size_t i = 0; (t = interface_directory_get_type(&interfaces, i)) != INTERFACE_TYPE_NONE; i++) {


		if (t == INTERFACE_TYPE_CELLULAR) {
			/* Get device name and a reference to the interface from the interface directory. */
			ICellular *cellular = (ICellular *)interface_directory_get_interface(&interfaces, i);
			const char *name = interface_directory_get_name(&interfaces, i);

			/* Get IMEI number of the device. */
			char imei[20];
			cellular_imei(cellular, imei);

			char operator[20];
			cellular_get_operator(cellular, operator);

			enum cellular_modem_status status;
			cellular_status(cellular, &status);

			table_print_row(cli->stream, device_cellular_table, (const union cli_table_cell_content []) {
				{.string = name},
				{.string = imei},
				{.string = cellular_modem_status_strings[status]},
				{.string = operator},
			});
		}
	}

	return 0;
}


int32_t device_cellular_cellularN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {

	size_t current_index = 0;
	enum interface_directory_type t;
	for (size_t i = 0; (t = interface_directory_get_type(&interfaces, i)) != INTERFACE_TYPE_NONE; i++) {
		if (t == INTERFACE_TYPE_CELLULAR) {
			if (index == current_index) {

				const char *name = interface_directory_get_name(&interfaces, i);
				if (node->name != NULL) {
					strcpy(node->name, name);
				}

				node->values = device_cellular_cellularN_values;

				return 0;

			}
			current_index++;
		}
	}
	return -1;
}




