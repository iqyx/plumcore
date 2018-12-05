/**
 * Copyright (c) 2014, Marek Koza (qyx@krtko.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "interface_flash.h"

#ifndef _SFFS_H_
#define _SFFS_H_


#define SFFS_MASTER_MAGIC 0x93827485
#define SFFS_METADATA_MAGIC 0x87985214
#define SFFS_LABEL_SIZE 8
#define SFFS_DIR_FILE_NAME_LENGTH 32

struct sffs;

struct sffs_file {
	uint32_t pos;
	uint16_t file_id;

	struct sffs *fs;
};

struct sffs_directory {
	uint32_t pos;
	struct sffs *fs;
};

enum sffs_dir_item_state {
	SFFS_DIR_ITEM_STATE_FREE,
	SFFS_DIR_ITEM_STATE_USED,
};

struct sffs_dir_item {
	enum sffs_dir_item_state state;
	uint32_t file_id;
	char file_name[SFFS_DIR_FILE_NAME_LENGTH];
};

struct sffs {
	uint32_t page_size;
	uint32_t sector_size;
	uint32_t sector_count;
	uint32_t data_pages_per_sector;
	uint32_t first_data_page;
	struct sffs_file root_dir;

	struct interface_flash *flash;

	char label[SFFS_LABEL_SIZE];
};

struct sffs_page {
	uint32_t sector;
	uint32_t page;

};

struct __attribute__((__packed__)) sffs_master_page {
	uint32_t magic;

	uint8_t page_size;
	uint8_t sector_size;
	uint16_t sector_count;

	uint8_t label[8];
};

struct sffs_info {
	uint32_t sectors_total;
	uint32_t sectors_erased;
	uint32_t sectors_used;
	uint32_t sectors_full;
	uint32_t sectors_dirty;
	uint32_t sectors_old;

	uint32_t pages_total;
	uint32_t pages_erased;
	uint32_t pages_used;
	uint32_t pages_old;

	uint32_t space_total;
	uint32_t space_used;
};

/* Erase state is set right after sector has been erased. Note that 0xFF is not
 * a valid state (value after erase), it must be set to SFFS_SECTOR_STATE_ERASED.
 * It indicates that no pages are used by data except metadata page, which is
 * initialized to default. */
#define SFFS_SECTOR_STATE_ERASED 0xDE

/* Used state means that at least one data page in this sector is marked as used
 * and at least one data page is marked as erased. Sectors with used state are
 * searched for file data pages or erased data pages. */
#define SFFS_SECTOR_STATE_USED 0xD6

/* All data pages in the sector are marked as used. There are no free erased
 * pages nor pages marked as old. This sector is not being touched except when
 * searching for file data pages */
#define SFFS_SECTOR_STATE_FULL 0x56

/* Sector contains at least one old data page and the rest of data pages is
 * marked as used. No free data pages are available. This sector is prepared to
 * be erased after all used pages are moved to another places. */
#define SFFS_SECTOR_STATE_DIRTY 0x46

/* Old sector contains only old pages, it can be safely erased */
#define SFFS_SECTOR_STATE_OLD 0x42


struct __attribute__((__packed__)) sffs_metadata_header {
	uint32_t magic;

	uint8_t state;
	uint8_t reserved;
	uint16_t reserved2;
};


/* Page is erased (full of 0xff) */
#define SFFS_PAGE_STATE_ERASED 0xB7

/* Reserved state means that this page is no longer available for new files and
 * write operation is in progress. */
#define SFFS_PAGE_STATE_RESERVED 0xB5

/* Page is marked as used (ie. it is part of a file). Pages marked as used are
 * searched when file contents is requested */
#define SFFS_PAGE_STATE_USED 0x35

/* Moving state means that this page was previously marked as used but now it is
 * being overwritten/moved to another location. New page (destination) must be
 * marked as reserved during this time. */
#define SFFS_PAGE_STATE_MOVING 0x34


/* All expired used pages are marked as old. They are prepared to be erased. */
#define SFFS_PAGE_STATE_OLD 0x24

/**
 * Metadata items are placed in metadata pages just after page header. Number of
 * metadata items is written in the header (it is determined during formatiing).
 * Metadata items can be spread along multiple metadata pages.
 */
struct __attribute__((__packed__)) sffs_metadata_item {
	/* Unique file identifier. It can be assigned using
	 * some sort of directory structure. This also means
	 * that maximum number of files on single filesystem
	 * can be 65536 */
	uint16_t file_id;

	/* Block index within one file. */
	uint16_t block;

	/* State of block. Can be one of the following:
	 * - erased - block is erased and ready to be used
	 * - used - block is used as part of a file
	 * - moving - block is prepared to be expired,
	 *                 new block allocation is in progress
	 * - expired - block is no longer used */
	uint8_t state;

	/* How much block space is used */
	uint16_t size;

	uint8_t reserved;
};


/**
 * File open mode constants
 */
#define SFFS_OVERWRITE 1
#define SFFS_APPEND 2
#define SFFS_READ 3


/**
 * Initialize SFFS filesystem structure and allocate all required resources.
 * This functions must be called prior to any other operation on the filesystem.
 *
 * @param fs A filesystem structure to initialize.
 *
 * @return SFFS_INIT_OK on success or
 *         SFFS_INIT_FAILED otherwise.
 */
int32_t sffs_init(struct sffs *fs);
#define SFFS_INIT_OK 0
#define SFFS_INIT_FAILED -1

/**
 * Mounts SFFS filesystem from a flash device. Mount operation fetches required
 * information from the flash, initializes page cache (if enabled), checks master
 * block if it is valid and marks the filesystem as mounted.
 *
 * @param fs A SFFS filesystem structure where the flash will be mounted to.
 * @param flash A flash device to be mounted.
 *
 * @return SFFS_MOUNT_OK on success or
 *         SFFS_MOUNT_FAILED otherwise.
 */
int32_t sffs_mount(struct sffs *fs, struct interface_flash *flash);
#define SFFS_MOUNT_OK 0
#define SFFS_MOUNT_FAILED -1

/**
 * Free SFFS filesystem and all allocated resources.
 *
 * @param fs A filesystem to free.
 *
 * @return SFFS_FREE_OK on success or
 *         SFFS_FREE_FAILED otherwise.
 */
int32_t sffs_free(struct sffs *fs);
#define SFFS_FREE_OK 0
#define SFFS_FREE_FAILED -1

/**
 * Clears all pages from filesystem cache.
 *
 * @param fs A filesystem with cache to clear.
 *
 * @return SFFS_CACHE_CLEAR_OK on success or
 *         SFFS_CACHE_CLEAR_FAILED odtherwise.
 */
int32_t sffs_cache_clear(struct sffs *fs);
#define SFFS_CACHE_CLEAR_OK 0
#define SFFS_CACHE_CLEAR_FAILED -1

/**
 * Create new SFFS filesystem on flash memory. Flash memory cannot be mounted
 * during this operation. Information about memory geometry is fetched directly
 * from the flash.
 *
 * @param flash A flash device to create SFFS filesystem on.
 *
 * @return SFFS_FORMAT_OK on success or
 *         SFFS_FORMAT_FAILED otherwise.
 */
int32_t sffs_format(struct interface_flash *flash);
#define SFFS_FORMAT_OK 0
#define SFFS_FORMAT_FAILED -1

int32_t sffs_sector_debug_print(struct sffs *fs, uint32_t sector);
#define SFFS_SECTOR_DEBUG_PRINT_OK 0
#define SFFS_SECTOR_DEBUG_PRINT_FAILED -1

/**
 * Print filesystem structure to stdout. It can help to visualize  how pages and
 * sectors are managed.
 *
 * @param fs A filesstem to print.
 *
 * @return SFFS_DEBUG_PRINT_OK.
 */
int32_t sffs_debug_print(struct sffs *fs);
#define SFFS_DEBUG_PRINT_OK 0
#define SFFS_DEBUG_PRINT_FAILED -1

/**
 * Perform various checks on metadata page header to find any inconsistencies.
 *
 * @param fs A SFFS filesystem to check.
 * @param header Metadata header structure to check (extracted from metadata page)
 *
 * @return SFFS_METADATA_HEADER_CHECK_OK if the header is valid or
 *         SFFS_METADATA_HEADER_CHECK_FAILED otherwise.
 */
int32_t sffs_metadata_header_check(struct sffs *fs, struct sffs_metadata_header *header);
#define SFFS_METADATA_HEADER_CHECK_OK 0
#define SFFS_METADATA_HEADER_CHECK_FAILED -1

/**
 * Try to fetch requested block of data from read cache. If requested data is not
 * found in the cache, read whole page from the flash.
 *
 * @param fs TODO
 * @param addr TODO
 * @param data TODO
 * @param len TODO
 *
 * @return TODO
 */
int32_t sffs_cached_read(struct sffs *fs, uint32_t addr, uint8_t *data, uint32_t len);
#define SFFS_CACHED_READ_OK 0
#define SFFS_CACHED_READ_FAILED -1

/**
 * Write one page to cache.
 *
 * @param fs A SFFS filesystem with cache.
 * @param addr Starting address of page to be written.
 * @param data Buffer with data.
 * @param len Length of data to be written.
 *
 * @return SFFS_CACHED_WRITE_OK on success or
 *         SFFS_CACHED_WRITE_FAILED otherwise.
 */
int32_t sffs_cached_write(struct sffs *fs, uint32_t addr, uint8_t *data, uint32_t len);
#define SFFS_CACHED_WRITE_OK 0
#define SFFS_CACHED_WRITE_FAILED -1

/**
 * Find data page for specified file_id and block.
 *
 * @param fs A SFFS filesystem.
 * @param file_id File to search for.
 * @param block Block index within the file to search from.
 * @param addr Pointer to address which will be set to found data block.
 *
 * @return SFFS_FIND_PAGE_OK on success,
 *         SFFS_FIND_PAGE_NOT_FOUND of no such file/block was found or
 *         SFFS_FIND_PAGE_FAILED otherwise.
 */
int32_t sffs_find_page(struct sffs *fs, uint32_t file_id, uint32_t block, struct sffs_page *page);
#define SFFS_FIND_PAGE_OK 0
#define SFFS_FIND_PAGE_NOT_FOUND -1
#define SFFS_FIND_PAGE_FAILED -2

/**
 * Try to find erased page suitable to be written with file contents.
 *
 * @param fs A SFFS filesystem.
 * @param page Pointer to page structure which will be filled if page is found.
 *
 * @return SFFS_FIND_ERASED_PAGE_OK on success,
 *         SFFS_FIND_ERASED_PAGE_NOT_FOUND if no erased page was found or
 *         SFFS_FIND_ERASED_PAGE_FAILED otherwise.
 */
int32_t sffs_find_erased_page(struct sffs *fs, struct sffs_page *page);
#define SFFS_FIND_ERASED_PAGE_OK 0
#define SFFS_FIND_ERASED_PAGE_NOT_FOUND -1
#define SFFS_FIND_ERASED_PAGE_FAILED -2

/**
 * Compute and return address of specified data page.
 *
 * @param fs A SFFS filesystem.
 * @param page A page to return address of.
 * @param addr Pointer to page address.
 *
 * @return SFFS_PAGE_ADDR_OK on success or
 *         SFFS_PAGE_ADDR_FAILED otherwise.
 */
int32_t sffs_page_addr(struct sffs *fs, struct sffs_page *page, uint32_t *addr);
#define SFFS_PAGE_ADDR_OK 0
#define SFFS_PAGE_ADDR_FAILED -1

int32_t sffs_sector_format(struct sffs *fs, uint32_t sector);
#define SFFS_SECTOR_FORMAT_OK 0
#define SFFS_SECTOR_FORMAT_FAILED -1

/**
 * Initiate garbage collection for a given sector. If the sector is marked as old,
 * it is being erased automatically. If the sector is dirty, garbage collector
 * finds out if it is worth it to move used page to other locations and erase
 * current dirty sector.
 *
 * @param A SFFS filesystem.
 * @param sector A sector to perform garbage collection on.
 *
 * @return SFFS_SECTOR_COLLECT_GARBAGE_OK on success or
 *         SFFS_SECTOR_COLLECT_GARBALE_FAILED otherwise.
 */
int32_t sffs_sector_collect_garbage(struct sffs *fs, uint32_t sector);
#define SFFS_SECTOR_COLLECT_GARBAGE_OK 0
#define SFFS_SECTOR_COLLECT_GARBAGE_FAILED -1

/**
 * Function called after every page metadata write to update sector metadata.
 * It iterates over all pages contained in specified sector and determines sector
 * state. State is written to sector header afterwards.
 *
 * @param fs A SFFS Filesystem.
 * @param sector Sector to update.
 *
 * @return SFFS_UPDATE_SECTOR_METADATA_OK on success or
 *         SFFS_UPDATE_SECTOR_METADATA_FAILED otherwise.
 */
int32_t sffs_update_sector_metadata(struct sffs *fs, uint32_t sector);
#define SFFS_UPDATE_SECTOR_METADATA_OK 0
#define SFFS_UPDATE_SECTOR_METADATA_FAILED -1

int32_t sffs_get_page_metadata(struct sffs *fs, struct sffs_page *page, struct sffs_metadata_item *item);
#define SFFS_GET_PAGE_METADATA_OK 0
#define SFFS_GET_PAGE_METADATA_FAILED -1

int32_t sffs_set_page_metadata(struct sffs *fs, struct sffs_page *page, struct sffs_metadata_item *item);
#define SFFS_SET_PAGE_MATEDATA_OK 0
#define SFFS_SET_PAGE_MATEDATA_FAILED -1

int32_t sffs_set_page_state(struct sffs *fs, struct sffs_page *page, uint8_t page_state);
#define SFFS_SET_PAGE_STATE_OK 0
#define SFFS_SET_PAGE_STATE_FAILED -1

/**
 * Check if specified file is opened.
 *
 * @param f SFFS file to be checked.
 *
 * @return SFFS_CHECK_FILE_OPENED_OK of the file is opened or
 *         SFFS_CHECK_FILE_OPENED_FAILED otherwise.
 */
int32_t sffs_check_file_opened(struct sffs_file *f);
#define SFFS_CHECK_FILE_OPENED_OK 0
#define SFFS_CHECK_FILE_OPENED_FAILED -1


/***************************** File manipulation functions, public API **********************************/

/**
 * Open a file by its ID.
 *
 * TODO: mode - read, overwrite, append.
 *
 * @param fs A SFFS filesystem with the file.
 * @param f SFFS file structure.
 * @param id ID of file to be opened.
 * @param mode Mode in whit the file will be opened, allowed values are
 *             SFFS_OVERWRITE, SFFS_APPEND and SFFS_READ.
 *
 * @return SFFS_OPEN_ID_OK on success or
 *         SFFS_OPEN_ID_FAILED otherwise.
 */
int32_t sffs_open_id(struct sffs *fs, struct sffs_file *f, uint32_t file_id, uint32_t mode);
#define SFFS_OPEN_ID_OK 0
#define SFFS_OPEN_ID_FAILED -1

/**
 * Close a previously opened file.
 *
 * @param f SFFS File to close.
 *
 * @return SFFS_CLOSE_OK on success or
 *         SFFS_CLOSE_FAILED otherwise.
 */
int32_t sffs_close(struct sffs_file *f);
#define SFFS_CLOSE_OK 0
#define SFFS_CLOSE_FAILED -1

/**
 * Write data buffer to an opened file at current position. Position in the file
 * is updated afterwards.
 *
 * @param f SFFS file to write data to.
 * @param buf Buffer containing data to be written.
 * @param len Length of data to be written.
 *
 * @return number of bytes written or -1 if error occured.
 */
int32_t sffs_write(struct sffs_file *f, unsigned char *buf, uint32_t len);

/**
 * Read up to len bytes of data from specified opened file starting at actual
 * position.
 *
 * @param f A SFFS File.
 * @param buf A buffer for read data.
 * @param len Length of data to be read from the file.
 *
 * @return -1 on error,
 *         0 if no more data can be read or
 *         number of bytes read.
 */
int32_t sffs_read(struct sffs_file *f, unsigned char *buf, uint32_t len);

/**
 * Update write/read position within an opened file.
 *
 * @param f SFFS file.
 *
 * @return SFFS_SEEK_OK on success or
 *         SFFS_SEEK_FAILED otherwise.
 */
int32_t sffs_seek(struct sffs_file *f, uint32_t pos);
#define SFFS_SEEK_OK 0
#define SFFS_SEEK_FAILED -1

/**
 * Removes all blocks for file @a file_id. File should be closed during removal.
 *
 * @param fs A SFFS filesystem.
 *
 * @return SFFS_FILE_REMOVE_ID_OK on success or
 *         SFFS_FILE_REMOVE_ID_FAILED otherise.
 */
int32_t sffs_file_remove_id(struct sffs *fs, uint32_t file_id);
#define SFFS_FILE_REMOVE_ID_OK 0
#define SFFS_FILE_REMOVE_ID_FAILED -1

/**
 * Compute size of specified file.
 *
 * @param fs A SFFS Filesystem.
 *
 * @return SFFS_FILE_SIZE_OK on success or
 *         SFFS_FILE_SIZE_FAILED otherwise.
 */
int32_t sffs_file_size(struct sffs *fs, struct sffs_file *f, uint32_t *size);
#define SFFS_FILE_SIZE_OK 0
#define SFFS_FILE_SIZE_FAILED -1

int32_t sffs_get_info(struct sffs *fs, struct sffs_info *info);
#define SFFS_GET_INFO_OK 0
#define SFFS_GET_INFO_FAILED -1

int32_t sffs_get_id_by_file_name(struct sffs *fs, const char *fname, uint32_t *id);
#define SFFS_GET_ID_BY_FILE_NAME_OK 0
#define SFFS_GET_ID_BY_FILE_NAME_FAILED -1
#define SFFS_GET_ID_BY_FILE_NAME_NOT_FOUND -2

int32_t sffs_add_file_name(struct sffs *fs, const char *fname, uint32_t *id);
#define SFFS_ADD_FILE_NAME_OK 0
#define SFFS_ADD_FILE_NAME_FAILED -1

int32_t sffs_file_remove(struct sffs *fs, const char *name);
#define SFFS_FILE_REMOVE_OK 0
#define SFFS_FILE_REMOVE_FAILED -1

int32_t sffs_open(struct sffs *fs, struct sffs_file *f, const char *fname, uint32_t mode);
#define SFFS_OPEN_OK 0
#define SFFS_OPEN_FAILED -1

int32_t sffs_directory_open(struct sffs *fs, struct sffs_directory *dir, const char *path);
#define SFFS_DIRECTORY_OPEN_OK 0
#define SFFS_DIRECTORY_OPEN_FAILED -1

int32_t sffs_directory_close(struct sffs_directory *dir);
#define SFFS_DIRECTORY_CLOSE_OK 0
#define SFFS_DIRECTORY_CLOSE_FAILED -1

int32_t sffs_directory_get_item(struct sffs_directory *dir, char *name, uint32_t max_len);
#define SFFS_DIRECTORY_GET_ITEM_OK 0
#define SFFS_DIRECTORY_GET_ITEM_FAILED -1


#endif
