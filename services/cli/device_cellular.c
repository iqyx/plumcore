#include "uhal/interfaces/cellular.h"
#include "cli_table_helper.h"
#include "services/cli.h"
#include "port.h"
#include "services/interfaces/servicelocator.h"


static int32_t device_cellular_cellularN_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)parser;
	(void)ctx;
	(void)value;
	(void)buf;
	(void)len;

	u_log(system_log, LOG_TYPE_INFO, "set enabled");

	return 0;
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

	Interface *interface;
	for (size_t i = 0; (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CELLULAR, i, &interface)) != ISERVICELOCATOR_RET_FAILED; i++) {

		/* Get device name and a reference to the interface from the interface directory. */
		ICellular *cellular = (ICellular *)interface;

		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);

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

	return 0;
}


static int32_t device_cellular_cellularN_create(struct treecli_parser *parser, uint32_t index, struct treecli_node *node, void *ctx) {
	(void)parser;
	(void)ctx;

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CELLULAR, index, &interface) == ISERVICELOCATOR_RET_OK) {
		const char *name = "";
		iservicelocator_get_name(locator, interface, &name);
		if (node->name != NULL) {
			strcpy(node->name, name);
		}
		node->values = &device_cellular_cellularN_values;
		return 0;


	}

	return -1;
}




