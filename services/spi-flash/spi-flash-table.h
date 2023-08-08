/**
 * @brief Flash ID table
 */

#define FLASH_TABLE_ITEM_OPS_COUNT 4
#define _256B 8
#define _4K 12
#define _8K 13
#define _16K 14
#define _32K 15
#define _64K 16
#define _1M 20
#define _2M 21
#define _4M 22
#define _8M 23
#define _16M 24
#define _32M 25

const struct flash_table_item flash_table[] = {
	{
		.id = 0x00014014,
		.mfg = "Spansion",
		.part = "S25FL208K",
		.size = _1M,
		.page = _256B,
		.sector = _4K,
		.block = _64K
	}, {
		.id = 0x00009d70,
		.mfg = "ISSI",
		.part = "IS25xP016D",
		.size = _2M,
		.page = _256B,
		.sector = _4K,
		.block = _64K
	}, {
		.id = 0x001f8701,
		.mfg = "Adesto",
		.part = "AT25SF321",
		.size = _4M,
		.page = _256B,
		.sector = _4K,
		.block = _64K
	}, {
		.id = 0x001f8501,
		.mfg = "Adesto",
		.part = "AT25SF081",
		.size = _1M,
		.page = _256B,
		.sector = _4K,
		.block = _64K
	}, {
		.id = 0
	}
};

