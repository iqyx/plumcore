#if 0

#include "umesh_l2_keymgr.h"
#include "port.h"
#include "module_umesh.h"
#include "lineedit.h"
#include "cli_table_helper.h"


/********************************* commands ***********************************/


static const struct cli_table_cell l2_keymgr_table[] = {
	{.type = TYPE_UINT32, .size = 10, .alignment = ALIGN_RIGHT}, /* my TID */
	{.type = TYPE_UINT32, .size = 10, .alignment = ALIGN_RIGHT}, /* peer TID */
	{.type = TYPE_STRING, .size = 10, .alignment = ALIGN_RIGHT}, /* state */
	{.type = TYPE_UINT32, .size = 10, .alignment = ALIGN_RIGHT}, /* state timeout */
	{.type = TYPE_KEY,    .size = 10, .alignment = ALIGN_RIGHT}, /* master TX key */
	{.type = TYPE_KEY,    .size = 10, .alignment = ALIGN_RIGHT}, /* master RX key */
	{.type = TYPE_END}
};


static int32_t ucli_umesh_l2_keymgr_print_table(struct treecli_parser *parser, void *exec_context, uint32_t *lines) {
	(void)exec_context;
	struct module_cli *cli = (struct module_cli *)parser->context;

	table_print_header(cli->stream, l2_keymgr_table, (const char *[]){
		"my tid",
		"peer tid",
		"state",
		"timeout",
		"master TX key",
		"master RX key"
	});
	table_print_row_separator(cli->stream, l2_keymgr_table);
	*lines = 2;

	for (size_t i = 0; i < umesh.key_manager.session_table_size; i++) {
		L2KeyManagerSession *session = &(umesh.key_manager.session_table[i]);
		if (session->state == UMESH_L2_KEYMGR_SESSION_STATE_EMPTY) {
			continue;
		}

		table_print_row(cli->stream, l2_keymgr_table, (const union cli_table_cell_content []) {
			{.uint32 = session->my_tid},
			{.int32 = session->peer_tid},
			{.string = umesh_l2_keymgr_states[session->state]},
			{.int32 = session->state_timeout_ms},
			{.key = session->master_tx_key},
			{.key = session->master_rx_key},
		});

		(*lines)++;
	}

	return 0;
}

static int32_t ucli_umesh_l2_keymgr_print_exec(struct treecli_parser *parser, void *exec_context) {

	uint32_t lines = 0;

	if (ucli_umesh_l2_keymgr_print_table(parser, exec_context, &lines) != 0) {
		return -1;
	}

	return 0;
}


static int32_t ucli_umesh_l2_keymgr_monitor_exec(struct treecli_parser *parser, void *exec_context) {

	struct module_cli *cli = (struct module_cli *)parser->context;
	uint32_t lines = 0;

	while (1) {
		/* Go to the start of the table. */
		for (uint32_t j = 0; j < lines; j++) {
			module_cli_output(ESC_CURSOR_UP, parser->context);
		}

		if (ucli_umesh_l2_keymgr_print_table(parser, exec_context, &lines) != 0) {
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


const struct treecli_command ucli_umesh_l2_keymgr_monitor = {
	.name = "monitor",
	.exec = ucli_umesh_l2_keymgr_monitor_exec,
};

const struct treecli_command ucli_umesh_l2_keymgr_print = {
	.name = "print",
	.exec = ucli_umesh_l2_keymgr_print_exec,
	.next = &ucli_umesh_l2_keymgr_monitor
};


/********************************* subnodes ***********************************/




/********************************** values ************************************/



#endif
