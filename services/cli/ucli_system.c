
static int32_t ucli_system_reboot(struct treecli_parser *parser, void *exec_context) {
	(void)parser;
	(void)exec_context;

	u_log(system_log, LOG_TYPE_WARN, "system: reboot requested");

	vTaskDelay(1000);
        *((unsigned long*)0xE000ED0C) = 0x05FA0004;
        while (1);

	return 0;
}


static int32_t ucli_system_poweroff(struct treecli_parser *parser, void *exec_context) {
	(void)parser;
	(void)exec_context;

	return 0;
}

