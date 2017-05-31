#include "port.h"
#include "module_umesh.h"
#include "lineedit.h"
#include "interface_mac.h"
#include "module_mac_csma.h"

/********************************* commands ***********************************/

#if 0

static int32_t print_mac_interface_header(struct treecli_parser *parser, void *exec_context, uint32_t *lines) {
	(void)exec_context;

	module_cli_output(
		ESC_BOLD
		"                                   RX                TX                          \r\n"
		"name            state       packets    bytes  packets    bytes                   \r\n"
		"---------------------------------------------------------------------------------\r\n"
		ESC_DEFAULT,
		parser->context
	);
	(*lines) += 3;

	return 0;
}


static int32_t print_mac_interface(struct treecli_parser *parser, void *exec_context, struct interface_mac *mac, uint32_t *lines) {
	(void)exec_context;

	char line[100];
	struct interface_mac_statistics stats;
	if (interface_mac_get_statistics(mac, &stats) != INTERFACE_MAC_GET_STATISTICS_OK) {
		snprintf(line, sizeof(line),
			"%-15s cannot get interface statistics\r\n",
			mac->descriptor.name
		);
		module_cli_output(line, parser->context);
		(*lines)++;
		return -1;
	}

	snprintf(line, sizeof(line),
		"%-15s %-10s %8u %8u %8u %8u\r\n",
		mac->descriptor.name,
		"UP",
		(unsigned int)stats.rx_packets,
		(unsigned int)stats.rx_bytes,
		(unsigned int)stats.tx_packets,
		(unsigned int)stats.tx_bytes
	);
	module_cli_output(line, parser->context);
	(*lines)++;

	return 0;
}


static int32_t ucli_interface_mac_print_exec(struct treecli_parser *parser, void *exec_context) {

	uint32_t lines = 0;
	print_mac_interface_header(parser, exec_context, &lines);
	//~ print_mac_interface(parser, exec_context, &(radio1_mac.iface), &lines);

	return 0;
}


static int32_t ucli_interface_mac_monitor_exec(struct treecli_parser *parser, void *exec_context) {

	struct module_cli *cli = (struct module_cli *)parser->context;
	uint32_t lines = 0;

	while (1) {
		/* Go to the start of the table. */
		for (uint32_t j = 0; j < lines; j++) {
			module_cli_output(ESC_CURSOR_UP, parser->context);
		}

		/** @todo replace */
		lines = 0;
		print_mac_interface_header(parser, exec_context, &lines);
		//~ print_mac_interface(parser, exec_context, &(radio1_mac.iface), &lines);

		uint8_t chr = 0;
		int16_t read = interface_stream_read_timeout(cli->stream, &chr, 1, 1000);
		if (read != 0) {
			break;
		}
	}

	return 0;
}


const struct treecli_command ucli_interface_mac_monitor = {
	.name = "monitor",
	.exec = ucli_interface_mac_monitor_exec,
};

const struct treecli_command ucli_interface_mac_print = {
	.name = "print",
	.exec = ucli_interface_mac_print_exec,
	.next = &ucli_interface_mac_monitor,
};


/********************************* subnodes ***********************************/




/********************************** values ************************************/


#endif

