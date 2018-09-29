#include "interfaces/cellular.h"
#include "cli_table_helper.h"
#include "cli.h"
#include "port.h"
#include "services/interfaces/servicelocator.h"

#define DNODE_INDEX(p, i) p->pos.levels[p->pos.depth + i].dnode_index
#define USSD_REQUEST_SIZE 16
char device_cellular_cellularN_ussd_request[USSD_REQUEST_SIZE] = {0};

static int32_t device_cellular_cellularN_enabled_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)parser;
	(void)ctx;
	(void)value;
	(void)buf;
	(void)len;

	u_log(system_log, LOG_TYPE_INFO, "set enabled");

	return 0;
}


static int32_t device_cellular_cellularN_ussd_request_set(struct treecli_parser *parser, void *ctx, struct treecli_value *value, void *buf, size_t len) {
	(void)ctx;
	(void)value;

	ServiceCli *cli = (ServiceCli *)parser->context;
	if (len < USSD_REQUEST_SIZE) {
		memcpy(device_cellular_cellularN_ussd_request, buf, len);
		device_cellular_cellularN_ussd_request[len] = '\0';
	} else {
		module_cli_output("error: USSD request string too long\r\n", cli);
	}

	return 0;
}


static int32_t device_cellular_cellularN_ussd_send(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;

	if (device_cellular_cellularN_ussd_request[0] == '\0') {
		module_cli_output("error: USSD request string not set\r\n", cli);
		return 1;
	}

	Interface *interface;
	if (iservicelocator_query_type_id(locator, ISERVICELOCATOR_TYPE_CELLULAR, DNODE_INDEX(parser, -1), &interface) != ISERVICELOCATOR_RET_OK) {
		return 1;
	}

	/* Get device name and a reference to the interface from the interface directory. */
	ICellular *cellular = (ICellular *)interface;

	char response[100] = {0};
	module_cli_output("running USSD \"", cli);
	module_cli_output(device_cellular_cellularN_ussd_request, cli);
	module_cli_output("\"\r\n", cli);
	if (cellular_run_ussd(cellular, device_cellular_cellularN_ussd_request, response, sizeof(response)) == CELLULAR_RET_OK) {
		module_cli_output(response, cli);
		module_cli_output("\r\n", cli);
		return 0;
	}
	module_cli_output("error: USSD failed\r\n", cli);
	return 1;
}


const struct treecli_node *device_cellular_cellularN = Node {
	Name "N",
	Commands {
		Command {
			Name "export",
		},
		End
	},
	Values {
		Value {
			Name "enabled",
			.set = device_cellular_cellularN_enabled_set,
			Type TREECLI_VALUE_BOOL,
		},
		End
	},
	Subnodes {
		Node {
			Name "ussd",
			Commands {
				Command {
					Name "send",
					Exec device_cellular_cellularN_ussd_send,
				},
				End
			},
			Values {
				Value {
					Name "request",
					.set = device_cellular_cellularN_ussd_request_set,
					Type TREECLI_VALUE_STR,
				},
				End
			},
		},
		End,
	},
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
		node->commands = device_cellular_cellularN->commands;
		node->values = device_cellular_cellularN->values;
		node->subnodes = device_cellular_cellularN->subnodes;

		return 0;


	}

	return -1;
}




