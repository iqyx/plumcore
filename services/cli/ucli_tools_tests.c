#include "umesh_l2_receive_tests.h"
#include "umesh_l2_send_tests.h"


static int32_t ucli_tools_tests_all(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	(void)parser;

	umesh_l2_receive_tests();
	umesh_l2_send_tests();

	return 0;
}

static int32_t ucli_tools_tests_l2send(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	(void)parser;

	umesh_l2_send_tests();

	return 0;
}

static int32_t ucli_tools_tests_l2receive(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	(void)parser;

	umesh_l2_receive_tests();

	return 0;
}


static int32_t ucli_tools_tests_ftsend(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	(void)parser;

	umesh_l2_file_transfer_send(&(umesh.file_transfer), "test_file.txt", 0xc206);

	return 0;
}


static int32_t ucli_tools_tests_ftreceive(struct treecli_parser *parser, void *exec_context) {
	(void)exec_context;
	(void)parser;

	umesh_l2_file_transfer_receive(&(umesh.file_transfer), "test_file.txt", 0xc206);

	return 0;
}


