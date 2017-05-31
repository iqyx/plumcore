static int32_t ucli_system_log_print(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;

	module_cli_output("Circular log buffer contents:\r\n", parser->context);
	log_cbuffer_print(system_log);

	return 0;
}



