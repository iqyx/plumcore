#include <malloc.h>

static int32_t ucli_system_memory_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;

	struct mallinfo mi;

	mi = mallinfo();

	char line[100];
	snprintf(line, sizeof(line), "Heap statistics:\r\n");
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  Total non-mmapped bytes:     %d bytes\r\n", mi.arena);
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  # of free chunks:            %d\r\n", mi.ordblks);
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  # of free fastbin blocks:    %d\r\n", mi.smblks);
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  # of mapped regions:         %d\r\n", mi.hblks);
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  Bytes in mapped regions:     %d bytes\r\n", mi.hblkhd);
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  Max. total allocated space:  %d bytes\r\n", mi.usmblks);
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  Free bytes held in fastbins: %d bytes\r\n", mi.fsmblks);
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  Total allocated space:       %d bytes\r\n", mi.uordblks);
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  Total free space:            %d bytes\r\n", mi.fordblks);
	module_cli_output(line, parser->context);

	snprintf(line, sizeof(line), "  Topmost releasable block:    %d\r\n", mi.keepcost);
	module_cli_output(line, parser->context);

	return 0;
}


