#if 0

#include "umesh_l2_keymgr.h"
#include "port.h"
#include "module_umesh.h"
#include "lineedit.h"
#include "cli_table_helper.h"
#include "cli_progressbar_helper.h"



/********************************* commands ***********************************/


static const struct cli_table_cell l2_filetransfer_table[] = {
	{.type = TYPE_UINT32, .size = 10, .alignment = ALIGN_RIGHT, .format = "%8x"}, /* peer TID */
	{.type = TYPE_STRING, .size = 15, .alignment = ALIGN_RIGHT}, /* state */
	{.type = TYPE_STRING, .size = 30, .alignment = ALIGN_RIGHT}, /* file name */
	{.type = TYPE_UINT32, .size = 10, .alignment = ALIGN_RIGHT}, /* file size */
	{.type = TYPE_STRING, .size = 30, .alignment = ALIGN_RIGHT}, /* progress */
	{.type = TYPE_END}
};


static int32_t ucli_umesh_l2_filetransfer_print_table(struct treecli_parser *parser, void *exec_context, uint32_t *lines) {
	(void)exec_context;
	struct module_cli *cli = (struct module_cli *)parser->context;

	table_print_header(cli->stream, l2_filetransfer_table, (const char *[]){
		"peer TID",
		"state",
		"file name",
		"file size",
		"progress"
	});
	table_print_row_separator(cli->stream, l2_filetransfer_table);
	*lines = 2;

	for (size_t i = 0; i < umesh.file_transfer.session_table_size; i++) {
		L2FtSession *session = &(umesh.file_transfer.session_table[i]);
		if (session->used == false) {
			continue;
		}

		FtSession *ft_session = session->file_transfer_session;

		char progress[50];
		cli_progressbar_render_simple(
			progress,
			sizeof(progress),
			20,
			ft_session->transferred_pieces,
			(ft_session->file_size_bytes - 1) / (ft_session->piece_size_blocks * ft_session->block_size_bytes) + 1
		);

		table_print_row(cli->stream, l2_filetransfer_table, (const union cli_table_cell_content []) {
			{.uint32 = session->peer_tid},
			{.string = "???"},
			{.string = ft_session->file_name},
			{.uint32 = ft_session->file_size_bytes},
			{.string = progress},
		});

		(*lines)++;
	}

	return 0;
}

static int32_t ucli_umesh_l2_filetransfer_print_exec(struct treecli_parser *parser, void *exec_context) {

	uint32_t lines = 0;

	if (ucli_umesh_l2_filetransfer_print_table(parser, exec_context, &lines) != 0) {
		return -1;
	}

	return 0;
}


static int32_t ucli_umesh_l2_filetransfer_monitor_exec(struct treecli_parser *parser, void *exec_context) {

	struct module_cli *cli = (struct module_cli *)parser->context;
	uint32_t lines = 0;

	while (1) {
		/* Go to the start of the table. */
		for (uint32_t j = 0; j < lines; j++) {
			module_cli_output(ESC_CURSOR_UP, parser->context);
		}

		if (ucli_umesh_l2_filetransfer_print_table(parser, exec_context, &lines) != 0) {
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


const struct treecli_command ucli_umesh_l2_filetransfer_monitor = {
	.name = "monitor",
	.exec = ucli_umesh_l2_filetransfer_monitor_exec,
};

const struct treecli_command ucli_umesh_l2_filetransfer_print = {
	.name = "print",
	.exec = ucli_umesh_l2_filetransfer_print_exec,
	.next = &ucli_umesh_l2_filetransfer_monitor
};


/********************************* subnodes ***********************************/




/********************************** values ************************************/



#endif
