#include "module_crypto_test.h"


static int32_t ucli_tools_crypto_benchmark(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	ServiceCli *cli = (ServiceCli *)parser->context;
	struct module_crypto_test test;

	if (module_crypto_test_init(&test, "crypto_test1", cli->stream) != MODULE_CRYPTO_TEST_INIT_OK) {
		module_cli_output("error: cannot instantiate crypto benchmarking suite\r\n", (void *)cli);
		return -1;
	}

	module_crypto_test_run(&test);
	while (module_crypto_test_running(&test)) {
		vTaskDelay(100);
	}
	/* TODO: free the test suite instance. */

	return 0;
}




