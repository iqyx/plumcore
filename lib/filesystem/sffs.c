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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "u_assert.h"
#include "u_log.h"
#include "sffs.h"
#include "interface_flash.h"


#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))


int32_t sffs_init(struct sffs *fs) {
	if (u_assert(fs != NULL)) {
		return SFFS_INIT_FAILED;
	}

	/* nothing to initialize yet */
	return SFFS_INIT_OK;
}


int32_t sffs_mount(struct sffs *fs, struct interface_flash *flash) {
	if (u_assert(fs != NULL) ||
	    u_assert(flash != NULL)) {
		return SFFS_MOUNT_FAILED;
	}

	struct interface_flash_info info;
	if (interface_flash_get_info(flash, &info) != INTERFACE_FLASH_GET_INFO_OK) {
		return SFFS_MOUNT_FAILED;
	}

	fs->flash = flash;
	fs->page_size = info.page_size;
	fs->sector_size = info.sector_size;
	fs->sector_count = info.capacity / info.sector_size;
	fs->data_pages_per_sector =
		(info.sector_size - sizeof(struct sffs_metadata_header)) /
		(sizeof(struct sffs_metadata_item) + info.page_size);
	fs->first_data_page = info.sector_size / info.page_size - fs->data_pages_per_sector;

	/* TODO: check flash geometry and computed values */

	if (sffs_cache_clear(fs) != SFFS_CACHE_CLEAR_OK) {
		return SFFS_MOUNT_FAILED;
	}

	/* Find first page of file "0", it should contain filesystem metadata */
	struct sffs_master_page master;
	struct sffs_file f;
	if (sffs_open_id(fs, &f, 0, SFFS_READ) != SFFS_OPEN_ID_OK) {
		return SFFS_MOUNT_FAILED;
	}
	sffs_read(&f, (unsigned char *)&master, sizeof(master));
	sffs_close(&f);

	/* TODO: check SFFS master page for validity */
	/* TODO: fetch filesystem label */

	/* Open root file directory. Do not check if it was successful. */
	sffs_open_id(fs, &(fs->root_dir), 1, SFFS_READ);


	return SFFS_MOUNT_OK;
}


int32_t sffs_free(struct sffs *fs) {
	if (u_assert(fs != NULL)) {
		return SFFS_FREE_FAILED;
	}

	sffs_close(&(fs->root_dir));

	return SFFS_FREE_OK;
}


int32_t sffs_cache_clear(struct sffs *fs) {
	if (u_assert(fs != NULL)) {
		return SFFS_CACHE_CLEAR_FAILED;
	}

	return SFFS_CACHE_CLEAR_OK;
}


int32_t sffs_format(struct interface_flash *flash) {
	if (u_assert(flash != NULL)) {
		return SFFS_FORMAT_FAILED;
	}

	struct interface_flash_info info;
	interface_flash_get_info(flash, &info);

	/* we need to simulate filesystem structure somehow (sector format functions
	 * operate on mounted filesystem). For this purpose we mount the filesystem
	 * using local sffs structure. Mount fails as no master page can be read,
	 * we ignore this silently, return code is intentionally unchecked. */
	struct sffs fs;
	sffs_mount(&fs, flash);

	/* now iterate over all sectors and format them */
	for (uint32_t sector = 0; sector < fs.sector_count; sector++) {
		sffs_sector_format(&fs, sector);
	}
	/* TODO: write master page */

	return SFFS_FORMAT_OK;
}


int32_t sffs_sector_debug_print(struct sffs *fs, uint32_t sector) {
	if (u_assert(fs != NULL) ||
	    u_assert(sector < fs->sector_count)) {
		return SFFS_SECTOR_DEBUG_PRINT_FAILED;
	}

	struct sffs_metadata_header header;
	/* TODO: check return value */
	sffs_cached_read(fs, sector * fs->sector_size, (uint8_t *)&header, sizeof(header));

	char sector_state = '?';
	if (header.state == SFFS_SECTOR_STATE_ERASED) sector_state = ' ';
	if (header.state == SFFS_SECTOR_STATE_USED) sector_state = 'U';
	if (header.state == SFFS_SECTOR_STATE_FULL) sector_state = 'F';
	if (header.state == SFFS_SECTOR_STATE_DIRTY) sector_state = 'D';
	if (header.state == SFFS_SECTOR_STATE_OLD) sector_state = 'O';
	printf("%04u [%c]: ", (unsigned int)sector, sector_state);

	for (uint32_t i = 0; i < fs->data_pages_per_sector; i++) {
		uint32_t item_pos = sector * fs->sector_size + sizeof(struct sffs_metadata_header) + i * sizeof(struct sffs_metadata_item);
		struct sffs_metadata_item item;
		/* TODO: check return value */
		sffs_cached_read(fs, item_pos, (uint8_t *)&item, sizeof(struct sffs_metadata_item));

		char page_state = '?';
		if (item.state == SFFS_PAGE_STATE_ERASED) page_state = ' ';
		if (item.state == SFFS_PAGE_STATE_USED) page_state = 'U';
		if (item.state == SFFS_PAGE_STATE_MOVING) page_state = 'M';
		if (item.state == SFFS_PAGE_STATE_RESERVED) page_state = 'R';
		if (item.state == SFFS_PAGE_STATE_OLD) page_state = 'O';
		printf("[%c] ", page_state);
	}
	printf("\n");

	return SFFS_SECTOR_DEBUG_PRINT_OK;
}


int32_t sffs_debug_print(struct sffs *fs) {
	if (u_assert(fs != NULL)) {
		return SFFS_DEBUG_PRINT_FAILED;
	}

	for (uint32_t sector = 0; sector < fs->sector_count; sector++) {
		sffs_sector_debug_print(fs, sector);
	}
	printf("\n");

	return SFFS_DEBUG_PRINT_OK;
}

int32_t sffs_metadata_header_check(struct sffs *fs, struct sffs_metadata_header *header) {
	if (u_assert(header != NULL) ||
	    u_assert(fs != NULL)) {
		return SFFS_METADATA_HEADER_CHECK_FAILED;
	}

	/* check magic number */
	if (header->magic != SFFS_METADATA_MAGIC) {
		return SFFS_METADATA_HEADER_CHECK_FAILED;
	}

	return SFFS_METADATA_HEADER_CHECK_OK;
}


int32_t sffs_cached_read(struct sffs *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	if (u_assert(fs != NULL) ||
	    u_assert(data != NULL)) {
		return SFFS_CACHED_READ_FAILED;
	}

	if (interface_flash_page_read(fs->flash, addr, data, len) != INTERFACE_FLASH_PAGE_READ_OK) {
		return SFFS_CACHED_READ_FAILED;
	}

	return SFFS_CACHED_READ_OK;
}


int32_t sffs_cached_write(struct sffs *fs, uint32_t addr, uint8_t *data, uint32_t len) {
	if (u_assert(fs != NULL) ||
	    u_assert(data != NULL)) {
		return SFFS_CACHED_WRITE_FAILED;
	}

	if (interface_flash_page_write(fs->flash, addr, data, len) != INTERFACE_FLASH_PAGE_WRITE_OK) {
		return SFFS_CACHED_WRITE_FAILED;
	}

	return SFFS_CACHED_WRITE_OK;
}


int32_t sffs_find_page(struct sffs *fs, uint32_t file_id, uint32_t block, struct sffs_page *page) {
	if (u_assert(fs != NULL) ||
	    u_assert(page != NULL)) {
		return SFFS_FIND_PAGE_FAILED;
	}

	/* first we need to iterate over all sectors in the flash */
	for (uint32_t sector = 0; sector < fs->sector_count; sector++) {
		struct sffs_metadata_header header;
		/* TODO: check return value */
		sffs_cached_read(fs, sector * fs->sector_size, (uint8_t *)&header, sizeof(header));

		if (header.state == SFFS_SECTOR_STATE_ERASED) {
			continue;
		}

		for (uint32_t i = 0; i < fs->data_pages_per_sector; i++) {
			struct sffs_metadata_item item;
			sffs_get_page_metadata(fs, &(struct sffs_page){ .sector = sector, .page = i }, &item);

			/* check if page contains valid data for requested file */
			if (item.file_id == file_id && item.block == block && (item.state == SFFS_PAGE_STATE_USED || item.state == SFFS_PAGE_STATE_MOVING)) {
				page->sector = sector;
				page->page = i;
				return SFFS_FIND_PAGE_OK;
			}
		}
	}

	return SFFS_FIND_PAGE_NOT_FOUND;
}


int32_t sffs_find_erased_page(struct sffs *fs, struct sffs_page *page) {
	if (u_assert(fs != NULL) ||
	    u_assert(page != NULL)) {
		return SFFS_FIND_ERASED_PAGE_FAILED;
	}

	/* first we need to iterate over all sectors in the flash */
	for (uint32_t sector = 0; sector < fs->sector_count; sector++) {
		struct sffs_metadata_header header;
		/* TODO: check return value */
		sffs_cached_read(fs, sector * fs->sector_size, (uint8_t *)&header, sizeof(header));

		if (header.state == SFFS_SECTOR_STATE_DIRTY || header.state == SFFS_SECTOR_STATE_FULL) {
			continue;
		}

		for (uint32_t i = 0; i < fs->data_pages_per_sector; i++) {
			uint32_t item_pos = sector * fs->sector_size + sizeof(struct sffs_metadata_header) + i * sizeof(struct sffs_metadata_item);
			struct sffs_metadata_item item;
			/* TODO: check return value */
			sffs_cached_read(fs, item_pos, (uint8_t *)&item, sizeof(struct sffs_metadata_item));

			if (item.state == SFFS_PAGE_STATE_ERASED) {
				page->sector = sector;
				page->page = i;
				return SFFS_FIND_ERASED_PAGE_OK;
			}
		}
	}

	return SFFS_FIND_ERASED_PAGE_NOT_FOUND;
}


int32_t sffs_page_addr(struct sffs *fs, struct sffs_page *page, uint32_t *addr) {
	if (u_assert(page != NULL) ||
	    u_assert(addr != NULL)) {
		return SFFS_PAGE_ADDR_FAILED;
	}

	*addr = page->sector * fs->sector_size + (fs->first_data_page + page->page) * fs->page_size;

	return SFFS_PAGE_ADDR_OK;
}


int32_t sffs_sector_format(struct sffs *fs, uint32_t sector) {
	if (u_assert(fs != NULL) ||
	    u_assert(sector < fs->sector_count)) {
		return SFFS_SECTOR_FORMAT_FAILED;
	}

	interface_flash_sector_erase(fs->flash, sector * fs->sector_size);

	/* prepare and write sector header */
	struct sffs_metadata_header header;
	header.magic = SFFS_METADATA_MAGIC;
	header.state = SFFS_SECTOR_STATE_ERASED;
	/* TODO: fill other fields */
	interface_flash_page_write(fs->flash, fs->sector_size * sector, (uint8_t *)&header, sizeof(header));

	/* prepare and write sector metadata items */
	for (uint32_t i = 0; i < fs->data_pages_per_sector; i++) {
		struct sffs_metadata_item item;
		item.file_id = 0xffff;
		item.block = 0xffff;
		item.state = SFFS_PAGE_STATE_ERASED;
		item.size = 0xffff;

		/* sffs_set_page_metadata cannot be used here as the remaining sector
		 * metadata are not complete yet and function will fail during sector
		 * metadata update */
		interface_flash_page_write(fs->flash, fs->sector_size * sector + sizeof(header) + i * sizeof(item), (uint8_t *)&item, sizeof(item));
	}

	return SFFS_SECTOR_FORMAT_OK;
}


int32_t sffs_sector_collect_garbage(struct sffs *fs, uint32_t sector) {
	if (u_assert(fs != NULL) ||
	    u_assert(sector < fs->sector_count)) {
		return SFFS_SECTOR_COLLECT_GARBAGE_FAILED;
	}

	struct sffs_metadata_header header;
	if (sffs_cached_read(fs, sector * fs->sector_size, (uint8_t *)&header, sizeof(header)) != SFFS_CACHED_READ_OK) {
		return SFFS_UPDATE_SECTOR_METADATA_FAILED;
	}

	if (sffs_metadata_header_check(fs, &header) != SFFS_METADATA_HEADER_CHECK_OK) {
		return SFFS_UPDATE_SECTOR_METADATA_FAILED;
	}

	if (header.state == SFFS_SECTOR_STATE_OLD) {
		sffs_sector_format(fs, sector);
	}

	return SFFS_SECTOR_COLLECT_GARBAGE_OK;
}


int32_t sffs_update_sector_metadata(struct sffs *fs, uint32_t sector) {
	if (u_assert(fs != NULL) ||
	    u_assert(sector < fs->sector_count)) {
		return SFFS_UPDATE_SECTOR_METADATA_FAILED;
	}

	struct sffs_metadata_header header;
	if (sffs_cached_read(fs, sector * fs->sector_size, (uint8_t *)&header, sizeof(header)) != SFFS_CACHED_READ_OK) {
		return SFFS_UPDATE_SECTOR_METADATA_FAILED;
	}

	if (sffs_metadata_header_check(fs, &header) != SFFS_METADATA_HEADER_CHECK_OK) {
		return SFFS_UPDATE_SECTOR_METADATA_FAILED;
	}

	uint8_t old_state = header.state;

	uint32_t p_erased = 0;
	uint32_t p_reserved = 0;
	uint32_t p_used = 0;
	uint32_t p_moving = 0;
	uint32_t p_old = 0;


	for (uint32_t i = 0; i < fs->data_pages_per_sector; i++) {
		struct sffs_metadata_item item;
		sffs_get_page_metadata(fs, &(struct sffs_page){ .sector = sector, .page = i }, &item);

		if (item.state == SFFS_PAGE_STATE_ERASED) p_erased++;
		if (item.state == SFFS_PAGE_STATE_RESERVED) p_reserved++;
		if (item.state == SFFS_PAGE_STATE_USED) p_used++;
		if (item.state == SFFS_PAGE_STATE_MOVING) p_moving++;
		if (item.state == SFFS_PAGE_STATE_OLD) p_old++;
	}

	int update_ok = 0;

	if (p_erased == fs->data_pages_per_sector && p_reserved == 0 && p_used == 0 && p_moving == 0 && p_old == 0) {
		header.state = SFFS_SECTOR_STATE_ERASED;
		update_ok = 1;
	}

	if (p_erased > 0 && (p_reserved > 0 || p_used > 0 || p_moving > 0 || p_old > 0)) {
		header.state = SFFS_SECTOR_STATE_USED;
		update_ok = 1;
	}

	if (p_erased == 0 && p_old == 0) {
		header.state = SFFS_SECTOR_STATE_FULL;
		update_ok = 1;
	}

	if (p_erased == 0 && (p_reserved + p_used + p_moving + p_old) == fs->data_pages_per_sector) {
		header.state = SFFS_SECTOR_STATE_DIRTY;
		update_ok = 1;
	}

	if (p_old == fs->data_pages_per_sector) {
		header.state = SFFS_SECTOR_STATE_OLD;
		update_ok = 1;
	}

	if (update_ok == 1) {
		/* New sector state cannot be greater than the old one
		 * (otherwise the sector metadata would need to be erased first).
		 * This would indicate error in metadata manipulation. */
		if (u_assert(header.state <= old_state)) {
			return SFFS_UPDATE_SECTOR_METADATA_FAILED;
		}

		if (old_state != header.state) {

			if (sffs_cached_write(fs, sector * fs->sector_size, (uint8_t *)&header, sizeof(header)) != SFFS_CACHED_WRITE_OK) {
				return SFFS_UPDATE_SECTOR_METADATA_FAILED;
			}

			sffs_sector_collect_garbage(fs, sector);
		}

		return SFFS_UPDATE_SECTOR_METADATA_OK;
	}

	u_assert(0);
	return SFFS_UPDATE_SECTOR_METADATA_FAILED;
}


int32_t sffs_get_page_metadata(struct sffs *fs, struct sffs_page *page, struct sffs_metadata_item *item) {
	if (u_assert(fs != NULL) ||
	    u_assert(page != NULL) ||
	    u_assert(item != NULL)) {
		return SFFS_GET_PAGE_METADATA_FAILED;
	}

	uint32_t item_pos = page->sector * fs->sector_size + sizeof(struct sffs_metadata_header) + page->page * sizeof(struct sffs_metadata_item);
	sffs_cached_read(fs, item_pos, (uint8_t *)item, sizeof(struct sffs_metadata_item));

	return SFFS_GET_PAGE_METADATA_OK;
}


int32_t sffs_set_page_metadata(struct sffs *fs, struct sffs_page *page, struct sffs_metadata_item *item) {
	if (u_assert(fs != NULL) ||
	    u_assert(page != NULL) ||
	    u_assert(item != NULL)) {
		return SFFS_SET_PAGE_MATEDATA_FAILED;
	}

	uint32_t item_pos = page->sector * fs->sector_size + sizeof(struct sffs_metadata_header) + page->page * sizeof(struct sffs_metadata_item);
	sffs_cached_write(fs, item_pos, (uint8_t *)item, sizeof(struct sffs_metadata_item));

	sffs_update_sector_metadata(fs, page->sector);

	return SFFS_SET_PAGE_MATEDATA_OK;
}


int32_t sffs_set_page_state(struct sffs *fs, struct sffs_page *page, uint8_t page_state) {
	if (u_assert(fs != NULL) ||
	    u_assert(page != NULL)) {
		return SFFS_SET_PAGE_STATE_FAILED;
	}

	struct sffs_metadata_item item;
	sffs_get_page_metadata(fs, page, &item);
	item.state = page_state;
	sffs_set_page_metadata(fs, page, &item);

	return SFFS_SET_PAGE_STATE_OK;
}


int32_t sffs_check_file_opened(struct sffs_file *f) {
	if (u_assert(f != NULL)) {
		return SFFS_CHECK_FILE_OPENED_FAILED;
	}

	/* file is probably not opened. TODO: better check */
	if (f->file_id == 0 || f->fs == NULL) {
		return SFFS_CHECK_FILE_OPENED_FAILED;
	}

	return SFFS_CHECK_FILE_OPENED_OK;
}


int32_t sffs_open_id(struct sffs *fs, struct sffs_file *f, uint32_t file_id, uint32_t mode) {
	if (u_assert(fs != NULL) ||
	    u_assert(f != NULL) ||
	    u_assert(file_id != 0xffff)) {
		return SFFS_OPEN_ID_FAILED;
	}

	f->fs = fs;
	f->file_id = file_id;

	switch (mode) {
		case SFFS_OVERWRITE:
			/* remove old file first, new one will be created.
			 * We are not checking return value intentionally.
			 * Write pointer is set to the beginning. */
			sffs_file_remove_id(fs, file_id);
			f->pos = 0;
			break;

		case SFFS_APPEND:
			/* Determine end of the file and seek to that position. */
			sffs_file_size(fs, f, &(f->pos));
			break;

		case SFFS_READ:
			/* Set write pointer to the beginning. */
			f->pos = 0;
			break;

		default:
			f->fs = NULL;
			return SFFS_OPEN_ID_FAILED;
	}

	return SFFS_OPEN_ID_OK;
}


int32_t sffs_close(struct sffs_file *f) {
	if (u_assert(f != NULL)) {
		return SFFS_CLOSE_FAILED;
	}

	if (sffs_check_file_opened(f) != SFFS_CHECK_FILE_OPENED_OK) {
		return SFFS_CLOSE_FAILED;
	}

	f->file_id = 0;
	f->fs = NULL;

	return SFFS_CLOSE_OK;
}


int32_t sffs_write(struct sffs_file *f, unsigned char *buf, uint32_t len) {
	if (u_assert(f != NULL) ||
	    u_assert(buf != NULL)) {
		return -1;
	}

	if (sffs_check_file_opened(f) != SFFS_CHECK_FILE_OPENED_OK) {
		return -1;
	}

	/* Write buffer can span multiple flash blocks. We need to determine
	 * where the buffer starts and ends. */
	uint32_t b_start = f->pos / f->fs->page_size;
	uint32_t b_end = (f->pos + len - 1) / f->fs->page_size;

	/* now we can iterate over all flash pages which need to be modified */
	for (uint32_t i = b_start; i <= b_end; i++) {

		uint8_t page_data[f->fs->page_size];
		struct sffs_page page;
		uint32_t loaded_old = 0;
		uint32_t old_len = 0;

		if (sffs_find_page(f->fs, f->file_id, i, &page) == SFFS_FIND_PAGE_OK) {
			/* page is valid. Get its address and read from flash */
			uint32_t addr;
			sffs_page_addr(f->fs, &page, &addr);
			if (sffs_cached_read(f->fs, addr, page_data, sizeof(page_data)) != SFFS_CACHED_READ_OK) {
				return -1;
			}

			struct sffs_metadata_item md;
			sffs_get_page_metadata(f->fs, &page, &md);
			old_len = md.size;

			loaded_old = 1;
		} else {
			/* the file doesn't have allocated requested page. Create
			 * new one filled with zeroes */
			memset(page_data, 0x00, sizeof(page_data));
		}

		/* determine where data wants to be written */
		uint32_t data_start = f->pos;
		uint32_t data_end = f->pos + len - 1;

		/* crop to current page boundaries */
		data_start = MAX(data_start, i * f->fs->page_size);
		data_end = MIN(data_end, (i + 1) * f->fs->page_size - 1);

		/* get offset in the source buffer */
		uint32_t source_offset = data_start - f->pos;

		/* get offset in the destination buffer */
		uint32_t dest_offset = data_start % f->fs->page_size;

		/* length of data to be writte is the difference between cropped data */
		uint32_t dest_len = data_end - data_start + 1;

		//~ printf("writing buf[%d-%d] to page %d, offset %d, length %d\n", source_offset, source_offset + dest_len, i, dest_offset, dest_len);

		if (u_assert(source_offset < len) ||
		    u_assert(dest_offset < f->fs->page_size) ||
		    u_assert(dest_len <= f->fs->page_size) ||
		    u_assert(dest_len <= len)) {
			return -1;
		}

		/* TODO: write actual data */
		memcpy(&(page_data[dest_offset]), &(buf[source_offset]), dest_len);

		/* find new erased page and mark is as reserved (prepared for writing).
		 * Mark old page as moving. */
		struct sffs_page new_page;
		if (sffs_find_erased_page(f->fs, &new_page) != SFFS_FIND_ERASED_PAGE_OK) {
			return -1;
		}

		if (loaded_old) {
			sffs_set_page_state(f->fs, &page, SFFS_PAGE_STATE_MOVING);
		}
		sffs_set_page_state(f->fs, &new_page, SFFS_PAGE_STATE_RESERVED);

		/* Finally write modified page, set its state to used and set state of
		 * old page to old- */
		uint32_t addr;
		sffs_page_addr(f->fs, &new_page, &addr);
		if (sffs_cached_write(f->fs, addr, page_data, sizeof(page_data)) != SFFS_CACHED_WRITE_OK) {
			return -1;
		}

		struct sffs_metadata_item item;
		item.block = i;
		item.size = data_end % f->fs->page_size + 1;
		if (loaded_old && old_len > item.size) {
			item.size = old_len;
		}
		item.state = SFFS_PAGE_STATE_USED;
		item.file_id = f->file_id;
		sffs_set_page_metadata(f->fs, &new_page, &item);

		if (loaded_old) {
			sffs_set_page_state(f->fs, &page, SFFS_PAGE_STATE_OLD);
		}
	}

	f->pos += len;

	return len;
}


int32_t sffs_read(struct sffs_file *f, unsigned char *buf, uint32_t len) {
	if (u_assert(f != NULL) ||
	    u_assert(buf != NULL) ||
	    u_assert(len > 0)) {
		return -1;
	}

	if (sffs_check_file_opened(f) != SFFS_CHECK_FILE_OPENED_OK) {
		return -1;
	}

	uint32_t b_start = f->pos / f->fs->page_size;
	uint32_t b_end = (f->pos + len - 1) / f->fs->page_size;

	uint32_t bytes_read = 0;

	/* iterate over all flash pages which need to be read */
	for (uint32_t i = b_start; i <= b_end; i++) {


		uint8_t page_data[f->fs->page_size];
		struct sffs_page page;
		uint32_t dest_len;

		if (sffs_find_page(f->fs, f->file_id, i, &page) == SFFS_FIND_PAGE_OK) {
			uint32_t addr;
			sffs_page_addr(f->fs, &page, &addr);
			if (sffs_cached_read(f->fs, addr, page_data, sizeof(page_data)) != SFFS_CACHED_READ_OK) {
				return -1;
			}

			struct sffs_metadata_item item;
			sffs_get_page_metadata(f->fs, &page, &item);

			uint32_t file_crop = i * f->fs->page_size + item.size - 1;
			uint32_t data_start = f->pos;
			uint32_t data_end = f->pos + len - 1;

			/* crop to current page boundaries */
			data_start = MAX(data_start, i * f->fs->page_size);
			data_end = MIN(data_end, (i + 1) * f->fs->page_size - 1);
			data_end = MIN(data_end, file_crop);

			/* get offset in the source buffer */
			uint32_t source_offset = data_start - f->pos;

			/* get offset in the destination buffer */
			uint32_t dest_offset = data_start % f->fs->page_size;

			/* length of data to be writte is the difference between cropped data */
			dest_len = data_end - data_start + 1;

			if (u_assert(source_offset < len) ||
			    u_assert(dest_offset < f->fs->page_size) ||
			    u_assert(dest_len <= f->fs->page_size) ||
			    u_assert(dest_len <= len)) {
				return -1;
			}

			//~ printf("reading from page %d, offset %d, length %d to  to buf[%d-%d]\n", i, dest_offset, dest_len, source_offset, source_offset + dest_len);
			memcpy(&(buf[source_offset]), &(page_data[dest_offset]), dest_len);
		} else {
			/* no more bytes to read */
			break;
		}

		bytes_read += dest_len;
	}

	f->pos += bytes_read;

	return bytes_read;
}


int32_t sffs_seek(struct sffs_file *f, uint32_t pos) {
	if (u_assert(f != NULL)) {
		return SFFS_SEEK_OK;
	}

	if (sffs_check_file_opened(f) != SFFS_CHECK_FILE_OPENED_OK) {
		return SFFS_SEEK_FAILED;
	}

	f->pos = pos;

	return SFFS_SEEK_OK;
}


int32_t sffs_file_remove_id(struct sffs *fs, uint32_t file_id) {
	if (u_assert(fs != NULL)) {
		return SFFS_FILE_REMOVE_ID_FAILED;
	}

	/* cannot remove filesystem metadata file */
	if (file_id == 0) {
		return SFFS_FILE_REMOVE_ID_FAILED;
	}

	/* iterate over file blocks until first find fails, mark all found blocks
	 * as old (remove from file).
	 * TODO: this action should probably be done in a safer way, interrupting
	 * deletion sequence will cause garbage to be left in the filesystem
	 * (file tail) */
	uint32_t block = 0;
	struct sffs_page page;
	while (sffs_find_page(fs, file_id, block, &page) == SFFS_FIND_PAGE_OK) {
		sffs_set_page_state(fs, &page, SFFS_PAGE_STATE_OLD);
		block++;
	}

	return SFFS_FILE_REMOVE_ID_OK;
}


int32_t sffs_file_size(struct sffs *fs, struct sffs_file *f, uint32_t *size) {
	if (u_assert(fs != NULL)) {
		return SFFS_FILE_SIZE_FAILED;
	}

	uint32_t file_id = f->file_id;
	uint32_t block = 0;
	uint32_t total_size = 0;
	struct sffs_page page;
	while (sffs_find_page(fs, file_id, block, &page) == SFFS_FIND_PAGE_OK) {
		struct sffs_metadata_item item;
		sffs_get_page_metadata(fs, &page, &item);
		total_size += item.size;

		block++;
	}
	*size = total_size;

	return SFFS_FILE_SIZE_OK;
}


int32_t sffs_get_info(struct sffs *fs, struct sffs_info *info) {
	if (u_assert(fs != NULL) ||
	    u_assert(info != NULL)) {
		return SFFS_GET_INFO_FAILED;
	}

	memset(info, 0, sizeof(struct sffs_info));

	info->sectors_total = fs->sector_count;
	for (uint32_t sector = 0; sector < fs->sector_count; sector++) {
		struct sffs_metadata_header header;
		if (sffs_cached_read(fs, sector * fs->sector_size, (uint8_t *)&header, sizeof(header)) != SFFS_CACHED_READ_OK) {
			return SFFS_GET_INFO_FAILED;
		}

		switch (header.state) {
			case SFFS_SECTOR_STATE_ERASED:
				info->sectors_erased++;
				break;
			case SFFS_SECTOR_STATE_USED:
				info->sectors_used++;
				break;
			case SFFS_SECTOR_STATE_FULL:
				info->sectors_full++;
				break;
			case SFFS_SECTOR_STATE_DIRTY:
				info->sectors_dirty++;
				break;
			case SFFS_SECTOR_STATE_OLD:
				info->sectors_old++;
				break;
			default:
				break;
		}

		for (uint32_t i = 0; i < fs->data_pages_per_sector; i++) {
			uint32_t item_pos = sector * fs->sector_size + sizeof(struct sffs_metadata_header) + i * sizeof(struct sffs_metadata_item);
			struct sffs_metadata_item item;
			/* TODO: check return value */
			if (sffs_cached_read(fs, item_pos, (uint8_t *)&item, sizeof(struct sffs_metadata_item)) != SFFS_CACHED_READ_OK) {
				return SFFS_GET_INFO_FAILED;
			}

			switch (item.state) {
				case SFFS_PAGE_STATE_ERASED:
					info->pages_erased++;
					break;
				case SFFS_PAGE_STATE_USED:
				case SFFS_PAGE_STATE_MOVING:
				case SFFS_PAGE_STATE_RESERVED:
					info->pages_used++;
					break;
				case SFFS_PAGE_STATE_OLD:
					info->pages_old++;
					break;
				default:
					break;
			}
			info->pages_total++;
		}
	}

	info->space_total = fs->page_size * info->pages_total;
	info->space_used = fs->page_size * info->pages_used;

	return SFFS_GET_INFO_OK;
}


int32_t sffs_get_id_by_file_name(struct sffs *fs, const char *fname, uint32_t *id) {
	if (u_assert(fs!= NULL && fname != NULL && id != NULL)) {
		return SFFS_GET_ID_BY_FILE_NAME_FAILED;
	}

	/* Linear search. Don't laugh plz. */
	sffs_seek(&(fs->root_dir), 0);
	struct sffs_dir_item item;
	while (sffs_read(&(fs->root_dir), (uint8_t *)&item, sizeof(struct sffs_dir_item)) > 0) {
		/* Make sure the file name is terminated properly. */
		item.file_name[SFFS_DIR_FILE_NAME_LENGTH - 1] = '\0';

		if (item.state == SFFS_DIR_ITEM_STATE_USED) {
			if (!strcmp(fname, item.file_name)) {
				*id = item.file_id;
				return SFFS_GET_ID_BY_FILE_NAME_OK;
			}
		}
	}

	return SFFS_GET_ID_BY_FILE_NAME_NOT_FOUND;
}


int32_t sffs_add_file_name(struct sffs *fs, const char *fname, uint32_t *id) {
	if (u_assert(fs!= NULL && fname != NULL && id != NULL)) {
		return SFFS_ADD_FILE_NAME_FAILED;
	}

	/* If the filename is already present in the directory, return it. */
	if (sffs_get_id_by_file_name(fs, fname, id) == SFFS_GET_ID_BY_FILE_NAME_OK) {
		return SFFS_ADD_FILE_NAME_OK;
	}

	/* Find first free directory entry. */
	sffs_seek(&(fs->root_dir), 0);
	uint32_t pos = 0;
	struct sffs_dir_item item;
	while (sffs_read(&(fs->root_dir), (uint8_t *)&item, sizeof(struct sffs_dir_item)) > 0) {
		if (item.state == SFFS_DIR_ITEM_STATE_FREE) {
			item.state = SFFS_DIR_ITEM_STATE_USED;
			strlcpy(item.file_name, fname, SFFS_DIR_FILE_NAME_LENGTH);
			/* TODO: better id allocation scheme. */
			item.file_id = pos + 1000;
			*id = item.file_id;

			/* Go back one item and overwrite the current item. */
			/* TODO: do not use f->pos */
			sffs_seek(&(fs->root_dir), fs->root_dir.pos - sizeof(struct sffs_dir_item));
			sffs_write(&(fs->root_dir), (uint8_t *)&item, sizeof(struct sffs_dir_item));
			return SFFS_ADD_FILE_NAME_OK;
		}
		pos++;
	}

	/* No such file name was found, append it. Current position is at the end. */
	item.state = SFFS_DIR_ITEM_STATE_USED;
	strlcpy(item.file_name, fname, SFFS_DIR_FILE_NAME_LENGTH);
	item.file_id = pos + 1000;
	*id = item.file_id;

	sffs_write(&(fs->root_dir), (uint8_t *)&item, sizeof(struct sffs_dir_item));
	return SFFS_ADD_FILE_NAME_OK;
}


int32_t sffs_file_remove(struct sffs *fs, const char *name) {
	if (u_assert(fs!= NULL)) {
		return SFFS_FILE_REMOVE_FAILED;
	}

	sffs_seek(&(fs->root_dir), 0);
	struct sffs_dir_item item;
	while (sffs_read(&(fs->root_dir), (uint8_t *)&item, sizeof(struct sffs_dir_item)) > 0) {
		if (!strcmp(name, item.file_name) && item.state == SFFS_DIR_ITEM_STATE_USED) {
			sffs_file_remove_id(fs, item.file_id);

			item.state = SFFS_DIR_ITEM_STATE_FREE;
			item.file_id = 0;
			item.file_name[0] = '\0';

			/* Go back one item and overwrite the current item. */
			/* TODO: do not use f->pos */
			sffs_seek(&(fs->root_dir), fs->root_dir.pos - sizeof(struct sffs_dir_item));
			sffs_write(&(fs->root_dir), (uint8_t *)&item, sizeof(struct sffs_dir_item));
		}
	}

	return SFFS_FILE_REMOVE_OK;
}


int32_t sffs_open(struct sffs *fs, struct sffs_file *f, const char *fname, uint32_t mode) {
	if (u_assert(fs!= NULL && f != NULL && fname != NULL)) {
		return SFFS_OPEN_FAILED;
	}

	uint32_t id;
	if (mode == SFFS_READ) {
		if (sffs_get_id_by_file_name(fs, fname, &id) != SFFS_GET_ID_BY_FILE_NAME_OK) {
			/* Cannot find existing file. */
			return SFFS_OPEN_FAILED;
		}
	} else {
		if (sffs_add_file_name(fs, fname, &id) != SFFS_ADD_FILE_NAME_OK) {
			/* Cannot create new file. */
			return SFFS_OPEN_FAILED;
		}
	}


	/* ID is valid now. */
	if (sffs_open_id(fs, f, id, mode) == SFFS_OPEN_ID_OK) {
		return SFFS_OPEN_OK;
	} else {
		return SFFS_OPEN_FAILED;
	}
}


int32_t sffs_directory_open(struct sffs *fs, struct sffs_directory *dir, const char *path) {
	if (u_assert(fs != NULL && dir != NULL && path != NULL)) {
		return SFFS_DIRECTORY_OPEN_FAILED;
	}

	dir->fs = fs;
	dir->pos = 0;

	return SFFS_DIRECTORY_OPEN_OK;
}


int32_t sffs_directory_close(struct sffs_directory *dir) {
	if (u_assert(dir != NULL)) {
		return SFFS_DIRECTORY_CLOSE_FAILED;
	}

	/* Directory is not opened. */
	if (dir->fs == NULL) {
		return SFFS_DIRECTORY_CLOSE_FAILED;
	}
	dir->pos = 0;
	dir->fs = NULL;

	return SFFS_DIRECTORY_CLOSE_OK;
}


int32_t sffs_directory_get_item(struct sffs_directory *dir, char *name, uint32_t max_len) {
	if (u_assert(dir != NULL && name != NULL && max_len > 0)) {
		return SFFS_DIRECTORY_GET_ITEM_FAILED;
	}

	/* Try to read directory entry from the current position. */
	sffs_seek(&(dir->fs->root_dir), dir->pos * sizeof(struct sffs_dir_item));
	struct sffs_dir_item item;
	while (sffs_read(&(dir->fs->root_dir), (uint8_t *)&item, sizeof(struct sffs_dir_item)) > 0) {
		dir->pos++;
		/* Go to next item if this one is not used. */
		if (item.state != SFFS_DIR_ITEM_STATE_USED) {
			continue;
		} else {
			strlcpy(name, item.file_name, max_len);
			return SFFS_DIRECTORY_GET_ITEM_OK;
		}
	}
	return SFFS_DIRECTORY_GET_ITEM_FAILED;
}
