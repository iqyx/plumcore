/********************************* subnodes ***********************************/

const struct treecli_node ucli_umesh_l2_filetransfer = {
	.name = "l2-file-transfer",
	//~ .commands = &ucli_umesh_l2_filetransfer_print,
};

const struct treecli_node ucli_umesh_l2_keymgr = {
	.name = "l2-keymgr",
	//~ .commands = &ucli_umesh_l2_keymgr_print,
	.next = &ucli_umesh_l2_filetransfer
};

const struct treecli_node ucli_umesh_nbtable = {
	.name = "nbtable",
	//~ .commands = &ucli_umesh_nbtable_print,
	.next = &ucli_umesh_l2_keymgr
};


/********************************* commands ***********************************/





/********************************** values ************************************/
