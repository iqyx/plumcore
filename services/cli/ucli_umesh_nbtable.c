#if 0

#include "umesh_l2_nbtable.h"
#include "port.h"
#include "module_umesh.h"
#include "lineedit.h"


/********************************* commands ***********************************/


static int32_t ucli_umesh_nbtable_print_table(struct treecli_parser *parser, void *exec_context, uint32_t *lines) {
	(void)exec_context;

	module_cli_output(
		ESC_BOLD
		ESC_ERASE_LINE_END "\r\n"
		ESC_ERASE_LINE_END "                                                                      RX all          RX dropped         shared key       \r\n"
		ESC_ERASE_LINE_END "     TID       state   state_t  beacon_t    rssi       fei  lqi  packets    bytes  packets    bytes  TX        RX         \r\n"
		ESC_ERASE_LINE_END "--------------------------------------------------------------------------------------------------------------------------\r\n"
		ESC_DEFAULT,
		parser->context
	);
	*lines = 4;

	for (uint32_t i = 0; i < umesh.nbtable.size; i++) {
		struct umesh_l2_nbtable_item *item = &(umesh.nbtable.nbtable[i]);

		char *state = "";
		char *item_esc = "";
		switch (item->state) {
			case UMESH_L2_NBTABLE_ITEM_STATE_EMPTY:
				state = "empty";
				continue;

			case UMESH_L2_NBTABLE_ITEM_STATE_OLD:
				state = "old";
				break;

			case UMESH_L2_NBTABLE_ITEM_STATE_NEW:
				state = "new";
				break;

			case UMESH_L2_NBTABLE_ITEM_STATE_VALID:
				state = "valid";
				break;

			case UMESH_L2_NBTABLE_ITEM_STATE_GUARD:
				state = "guard";
				item_esc = ESC_COLOR_FG_RED;
				break;

			default:
				state = "?";
				break;
		}


		char line[160];
		snprintf(line, sizeof(line),
			ESC_ERASE_LINE_END "%s%08x%12s%8ums%8ums%5ddBm%8dHz%4u%%%9u%9u%9u%9u  %s\r\n",
			item_esc,
			(unsigned int)item->tid,
			state,
			(unsigned int)item->state_timeout_msec,
			(unsigned int)item->unreachable_time_msec,
			(int)(item->rssi_10dBm / 10),
			(int)(item->fei_hz),
			(unsigned int)(item->lqi_percent),
			(unsigned int)(item->rx_packets),
			(unsigned int)(item->rx_bytes),
			(unsigned int)(item->rx_packets_dropped),
			(unsigned int)(item->rx_bytes_dropped),
			ESC_DEFAULT
		);
		module_cli_output(line, parser->context);

		(*lines)++;
	}

	return 0;
}

static int32_t ucli_umesh_nbtable_print_exec(struct treecli_parser *parser, void *exec_context) {

	uint32_t lines = 0;

	if (ucli_umesh_nbtable_print_table(parser, exec_context, &lines) != 0) {
		return -1;
	}

	return 0;
}


static int32_t ucli_umesh_nbtable_monitor_exec(struct treecli_parser *parser, void *exec_context) {

	struct module_cli *cli = (struct module_cli *)parser->context;
	uint32_t lines = 0;

	while (1) {
		/* Go to the start of the table. */
		for (uint32_t j = 0; j < lines; j++) {
			module_cli_output(ESC_CURSOR_UP, parser->context);
		}

		if (ucli_umesh_nbtable_print_table(parser, exec_context, &lines) != 0) {
			return -1;
		}

		uint8_t chr = 0;
		int16_t read = interface_stream_read_timeout(cli->stream, &chr, 1, 1000);
		if (read != 0) {
			break;
		}
	}

	return 0;
}


const struct treecli_command ucli_umesh_nbtable_monitor = {
	.name = "monitor",
	.exec = ucli_umesh_nbtable_monitor_exec,
};

const struct treecli_command ucli_umesh_nbtable_print = {
	.name = "print",
	.exec = ucli_umesh_nbtable_print_exec,
	.next = &ucli_umesh_nbtable_monitor
};


/********************************* subnodes ***********************************/




/********************************** values ************************************/


#endif
